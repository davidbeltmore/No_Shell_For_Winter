#include "EFClothingImmutableGraphProvider.h"
#include "EFClothingSurfaceBinding.h"
#include "OptimusDeformer.h"
#include "ComputeFramework/ComputeGraph.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/SecureHash.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformerInstance.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"
#include "UObject/UnrealType.h"

static TAutoConsoleVariable<int32> CVarImmutableBindingBuffers(
    TEXT("ef.ClothingMorph.ImmutableBindingBuffers"), 1,
    TEXT("Reuse immutable surface-binding arrays. Applied at the next garment installation; 0 retains the stock provider."));

static TAutoConsoleVariable<int32> CVarPreparedBinding(
    TEXT("ef.ClothingMorph.PreparedBinding"), 1,
    TEXT("Reuse validated binding installation data while a matching live provider owns it. Next installation only."));

struct FEFClothingImmutableGraphBuffers
{
    struct FArray
    {
        int32 Offset = 0;
        int32 Stride = 0;
        TArray<uint8> Bytes;
        // Only the render thread reads/writes pooled resources.
        TRefCountPtr<FRDGPooledBuffer> Pooled;
    };
    TArray<FArray> Arrays;
    ~FEFClothingImmutableGraphBuffers()
    {
        TArray<TRefCountPtr<FRDGPooledBuffer>> Release;
        for (auto& Array : Arrays) Release.Add(MoveTemp(Array.Pooled));
        if (!IsInRenderingThread())
        {
            ENQUEUE_RENDER_COMMAND(EFReleaseImmutableClothingBuffers)(
                [Release = MoveTemp(Release)](FRHICommandListImmediate&) mutable { Release.Reset(); });
        }
    }
};

struct FEFClothingPreparedGraphBinding
{
    struct FProvider
    {
        int32 GraphIndex = 0;
        int32 ProviderIndex = 0;
        TWeakObjectPtr<UComputeGraph> ComputeGraph;
        TWeakObjectPtr<UObject> Interface;
        TArray<UEFClothingImmutableGraphProvider::FVariable> Variables;
        int32 ParameterBytes = 0;
        TSharedPtr<FEFClothingImmutableGraphBuffers, ESPMode::ThreadSafe> Buffers;
    };
    TWeakObjectPtr<const UEFClothingSurfaceBinding> Binding;
    TWeakObjectPtr<UOptimusDeformer> Graph;
    FGuid BuildGuid;
    FString CompileFingerprint;
    int32 GarmentLOD = INDEX_NONE;
    int32 BodyLOD = INDEX_NONE;
    int32 GraphCount = 0;
    TArray<FProvider> Providers;
    TMap<FName, int32> Integers;
};

namespace
{
using FBuffers = FEFClothingImmutableGraphBuffers;
using FBuffersPtr = TSharedPtr<FBuffers, ESPMode::ThreadSafe>;

// Weak lookup only: owners and in-flight proxies determine lifetime, never the cache.
// Limit the lookup even if many different outfits are simultaneously alive.
TMap<FString, TWeakPtr<FBuffers, ESPMode::ThreadSafe>> BufferLookup;
// No cache entry owns a UObject or keeps resources alive after the last provider.
TArray<TWeakPtr<FEFClothingPreparedGraphBinding, ESPMode::ThreadSafe>> PreparedLookup;

TArray<FOptimusDeformerInstanceExecInfo>* GetExecInfos(UOptimusDeformerInstance* Instance)
{
    const auto* Property = FindFProperty<FArrayProperty>(Instance->GetClass(), TEXT("ComputeGraphExecInfos"));
    const auto* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    return Inner && Inner->Struct->GetFName() == TEXT("OptimusDeformerInstanceExecInfo")
        ? Property->ContainerPtrToValuePtr<TArray<FOptimusDeformerInstanceExecInfo>>(Instance) : nullptr;
}

UObject* GetInterface(UComputeGraph* Graph, int32 Index)
{
    const auto* Property = FindFProperty<FArrayProperty>(Graph->GetClass(), TEXT("DataInterfaces"));
    const auto* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
    if (!Inner) return nullptr;
    FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(Graph));
    return Index >= 0 && Index < Values.Num() ? Inner->GetObjectPropertyValue(Values.GetRawPtr(Index)) : nullptr;
}

class FImmutableProxy final : public FComputeDataProviderRenderProxy
{
public:
    TArray<uint8> Parameters;
    FBuffersPtr Buffers;
    TArray<FRDGBufferSRVRef> SRVs;

    bool IsValid(const FValidationData& Data) const override
    {
        return Buffers.IsValid() && !Parameters.IsEmpty() && Parameters.Num() == Data.ParameterStructSize;
    }
    void AllocateResources(FRDGBuilder& Graph, const FAllocationData&) override
    {
        for (auto& Array : Buffers->Arrays)
        {
            FRDGBufferRef Buffer;
            if (Array.Pooled.IsValid()) Buffer = Graph.RegisterExternalBuffer(Array.Pooled);
            else
            {
                Buffer = Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
                    Array.Stride, FMath::Max(1, Array.Bytes.Num() / Array.Stride)), TEXT("EFClothing.ImmutableBinding"));
                Graph.QueueBufferUpload(Buffer, Array.Bytes.GetData(), Array.Bytes.Num(), ERDGInitialDataFlags::None);
                Array.Pooled = Graph.ConvertToExternalBuffer(Buffer);
            }
            SRVs.Add(Graph.CreateSRV(Buffer));
        }
    }
    void GatherDispatchData(const FDispatchData& Data) override
    {
        for (int32 I = 0; I < Data.NumInvocations; ++I)
        {
            uint8* Destination = Data.ParameterBuffer + Data.ParameterBufferOffset + Data.ParameterBufferStride * I;
            FMemory::Memcpy(Destination, Parameters.GetData(), Parameters.Num());
            for (int32 J = 0; J < SRVs.Num(); ++J)
                *reinterpret_cast<FRDGBufferSRVRef*>(Destination + Buffers->Arrays[J].Offset) = SRVs[J];
        }
    }
};

bool Prepare(UEFClothingImmutableGraphProvider* Adapter, UObject* Source, UObject* Interface, FString& Failure,
    const FBuffersPtr& Reuse = nullptr)
{
    // UE 5.8 publishes these layouts through reflection, but not public C++ classes.
    // Validate every field before using it; never include private Engine headers or patch assets.
    const auto* VariableProperty = FindFProperty<FArrayProperty>(Source->GetClass(), TEXT("Variables"));
    const auto* ParameterProperty = FindFProperty<FIntProperty>(Interface->GetClass(), TEXT("ParameterBufferSize"));
    const auto* Inner = VariableProperty ? CastField<FStructProperty>(VariableProperty->Inner) : nullptr;
    if (!Inner || !ParameterProperty) { Failure = TEXT("Graph provider reflection layout missing"); return false; }
    const auto* IdProperty = FindFProperty<FStructProperty>(Inner->Struct, TEXT("ValueId"));
    const auto* OffsetProperty = FindFProperty<FIntProperty>(Inner->Struct, TEXT("Offset"));
    const auto* TypeProperty = FindFProperty<FStructProperty>(Inner->Struct, TEXT("ValueType"));
    if (!IdProperty || IdProperty->Struct->GetFName() != TEXT("OptimusValueIdentifier")
        || !TypeProperty || TypeProperty->Struct->GetFName() != TEXT("ShaderValueTypeHandle") || !OffsetProperty)
    { Failure = TEXT("Graph variable reflection layout mismatch"); return false; }
    Adapter->ParameterBytes = ParameterProperty->GetPropertyValue_InContainer(Interface);
    if (Adapter->ParameterBytes <= 0 || Adapter->ParameterBytes > 65536)
    { Failure = TEXT("Graph parameter size outside adapter bounds"); return false; }
    auto Pending = MakeShared<FBuffers, ESPMode::ThreadSafe>();
    FMD5 Digest;
    int32 ReusedArrayCount = 0;
    FScriptArrayHelper Values(VariableProperty, VariableProperty->ContainerPtrToValuePtr<void>(Source));
    for (int32 I = 0; I < Values.Num(); ++I)
    {
        void* Row = Values.GetRawPtr(I);
        UEFClothingImmutableGraphProvider::FVariable Variable;
        Variable.Id = *IdProperty->ContainerPtrToValuePtr<FOptimusValueIdentifier>(Row);
        Variable.Offset = OffsetProperty->GetPropertyValue_InContainer(Row);
        const auto& ShaderValue = Adapter->Instance->GetShaderValue(Variable.Id);
        Variable.Size = ShaderValue.ShaderValue.Num();
        if (Variable.Offset < 0 || Variable.Offset + Variable.Size > Adapter->ParameterBytes)
        { Failure = FString::Printf(TEXT("Graph variable %s bounds mismatch (%d + %d > %d)"),
            *Variable.Id.Name.ToString(), Variable.Offset, Variable.Size, Adapter->ParameterBytes); return false; }
        Adapter->Variables.Add(Variable);
        const auto& Type = *TypeProperty->ContainerPtrToValuePtr<FShaderValueTypeHandle>(Row);
        auto DataType = FOptimusDataTypeRegistry::Get().FindType(Type);
        if (!DataType.IsValid()) { Failure = TEXT("Graph data type unresolved"); return false; }
        const int32 ArrayCount = DataType->GetNumArrays();
        if (ArrayCount != ShaderValue.ArrayList.Num())
        { Failure = TEXT("Graph array count mismatch"); return false; }
        for (int32 J = 0; J < ArrayCount; ++J)
        {
            // Only owned, installation-time binding arrays are eligible.
            if (!Variable.Id.Name.ToString().StartsWith(TEXT("EF_")))
            { Failure = TEXT("Non-binding array rejected by immutable adapter"); return false; }
            auto& Array = Pending->Arrays.AddDefaulted_GetRef();
            Array.Offset = Variable.Offset + DataType->GetArrayShaderValueOffset(J);
            Array.Stride = DataType->GetArrayElementShaderValueSize(J);
            if (Reuse)
            {
                if (!Reuse->Arrays.IsValidIndex(ReusedArrayCount)
                    || Reuse->Arrays[ReusedArrayCount].Offset != Array.Offset
                    || Reuse->Arrays[ReusedArrayCount].Stride != Array.Stride)
                { Failure = TEXT("Prepared graph array layout changed"); return false; }
                ++ReusedArrayCount;
                continue;
            }
            Array.Bytes = ShaderValue.ArrayList[J].ArrayOfValues;
            if (Array.Offset < 0 || Array.Offset + sizeof(FRDGBufferSRVRef) > Adapter->ParameterBytes
                || Array.Stride <= 0 || Array.Bytes.Num() % Array.Stride != 0)
            { Failure = TEXT("Graph array layout mismatch"); return false; }
            Digest.Update(reinterpret_cast<const uint8*>(&Array.Offset), sizeof(Array.Offset));
            Digest.Update(reinterpret_cast<const uint8*>(&Array.Stride), sizeof(Array.Stride));
            const int32 ByteCount = Array.Bytes.Num();
            Digest.Update(reinterpret_cast<const uint8*>(&ByteCount), sizeof(ByteCount));
            Digest.Update(Array.Bytes.GetData(), ByteCount);
        }
    }
    if (Pending->Arrays.IsEmpty()) { Failure = TEXT("Surface graph contains no immutable arrays"); return false; }
    if (Reuse)
    {
        if (ReusedArrayCount != Reuse->Arrays.Num())
        { Failure = TEXT("Prepared graph array count changed"); return false; }
        Adapter->Buffers = Reuse;
        return true;
    }
    uint8 Hash[16]; Digest.Final(Hash);
    const FString Key = BytesToHex(Hash, 16);
    for (auto It = BufferLookup.CreateIterator(); It; ++It) if (!It.Value().IsValid()) It.RemoveCurrent();
    if (auto* Existing = BufferLookup.Find(Key)) Adapter->Buffers = Existing->Pin();
    if (!Adapter->Buffers)
    {
        Adapter->Buffers = Pending;
        if (BufferLookup.Num() < 128) BufferLookup.Add(Key, Pending);
    }
    return true;
}
}

bool UEFClothingImmutableGraphProvider::Install(UOptimusDeformerInstance* Instance, FString& Failure)
{
    if (!CVarImmutableBindingBuffers.GetValueOnGameThread()) return true;
    const auto* InfosProperty = FindFProperty<FArrayProperty>(Instance->GetClass(), TEXT("ComputeGraphExecInfos"));
    const auto* Inner = InfosProperty ? CastField<FStructProperty>(InfosProperty->Inner) : nullptr;
    if (!Inner || Inner->Struct->GetFName() != TEXT("OptimusDeformerInstanceExecInfo"))
    { Failure = TEXT("Optimus execution layout unavailable"); return false; }
    auto& Infos = *InfosProperty->ContainerPtrToValuePtr<TArray<FOptimusDeformerInstanceExecInfo>>(Instance);
    struct FReplacement { TObjectPtr<UComputeDataProvider>* Slot; UEFClothingImmutableGraphProvider* Value; };
    TArray<FReplacement> Replacements;
    for (auto& Info : Infos)
    {
        if (!Info.ComputeGraph) continue;
        const auto* InterfacesProperty = FindFProperty<FArrayProperty>(Info.ComputeGraph->GetClass(), TEXT("DataInterfaces"));
        const auto* ObjectProperty = InterfacesProperty ? CastField<FObjectPropertyBase>(InterfacesProperty->Inner) : nullptr;
        if (!ObjectProperty) { Failure = TEXT("Compute interface layout unavailable"); return false; }
        FScriptArrayHelper Interfaces(InterfacesProperty, InterfacesProperty->ContainerPtrToValuePtr<void>(Info.ComputeGraph));
        auto& Providers = Info.ComputeGraphInstance.GetDataProviders();
        if (Interfaces.Num() != Providers.Num()) { Failure = TEXT("Compute interface/provider mismatch"); return false; }
        for (int32 I = 0; I < Providers.Num(); ++I)
        {
            auto* Source = Providers[I].Get();
            if (!Source || Source->GetClass()->GetFName() != TEXT("OptimusGraphDataProvider")) continue;
            auto* Interface = ObjectProperty->GetObjectPropertyValue(Interfaces.GetRawPtr(I));
            if (!Interface) { Failure = TEXT("Compute graph interface missing"); return false; }
            auto* Adapter = NewObject<UEFClothingImmutableGraphProvider>(Instance);
            Adapter->Instance = Instance;
            if (!Prepare(Adapter, Source, Interface, Failure)) return false;
            Replacements.Add({ &Providers[I], Adapter });
        }
    }
    if (Replacements.IsEmpty()) { Failure = TEXT("No surface graph providers found"); return false; }
    // Commit only after all layouts have passed. A failure preserves every stock provider.
    for (const auto& Replacement : Replacements) *Replacement.Slot = Replacement.Value;
    return true;
}

bool UEFClothingImmutableGraphProvider::TryInstallPrepared(UOptimusDeformerInstance* Instance,
    UOptimusDeformer* Graph, const UEFClothingSurfaceBinding* Binding, const FEFClothingSurfaceLODPairBinding& Pair)
{
    check(IsInGameThread());
    if (!CVarImmutableBindingBuffers.GetValueOnGameThread() || !CVarPreparedBinding.GetValueOnGameThread()) return false;
    auto* Infos = GetExecInfos(Instance);
    if (!Infos) return false;
    PreparedLookup.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
    for (const auto& Entry : PreparedLookup)
    {
        auto Cached = Entry.Pin();
        if (!Cached || Cached->Binding.Get() != Binding || Cached->Graph.Get() != Graph
            || Cached->BuildGuid != Binding->BuildGuid || Cached->CompileFingerprint != Binding->GarmentCompileFingerprint
            || Cached->GarmentLOD != Pair.GarmentTopology.LODIndex || Cached->BodyLOD != Pair.BodyTopology.LODIndex
            || Cached->GraphCount != Infos->Num()) continue;
        int32 GraphProviderCount = 0;
        for (auto& Info : *Infos)
            for (const auto& Provider : Info.ComputeGraphInstance.GetDataProviders())
                if (Provider && Provider->GetClass()->GetFName() == TEXT("OptimusGraphDataProvider")) ++GraphProviderCount;
        if (GraphProviderCount != Cached->Providers.Num()) return false;
        TArray<UEFClothingImmutableGraphProvider*> Adapters;
        for (const auto& Previous : Cached->Providers)
        {
            if (!Infos->IsValidIndex(Previous.GraphIndex)) return false;
            auto& Info = (*Infos)[Previous.GraphIndex];
            if (!Previous.ComputeGraph.IsValid() || Info.ComputeGraph != Previous.ComputeGraph.Get()) return false;
            auto& Providers = Info.ComputeGraphInstance.GetDataProviders();
            if (!Providers.IsValidIndex(Previous.ProviderIndex)) return false;
            UObject* Source = Providers[Previous.ProviderIndex].Get();
            UObject* Interface = GetInterface(Info.ComputeGraph, Previous.ProviderIndex);
            if (!Source || Source->GetClass()->GetFName() != TEXT("OptimusGraphDataProvider")
                || !Interface || Interface != Previous.Interface.Get()) return false;
            auto* Adapter = NewObject<UEFClothingImmutableGraphProvider>(Instance);
            Adapter->Instance = Instance;
            FString Failure;
            if (!Prepare(Adapter, Source, Interface, Failure, Previous.Buffers)
                || Adapter->ParameterBytes != Previous.ParameterBytes
                || Adapter->Variables.Num() != Previous.Variables.Num()) return false;
            for (int32 I = 0; I < Adapter->Variables.Num(); ++I)
            {
                const auto& A = Adapter->Variables[I]; const auto& B = Previous.Variables[I];
                if (!(A.Id == B.Id) || A.Offset != B.Offset || A.Size != B.Size) return false;
            }
            Adapter->Prepared = Cached;
            Adapters.Add(Adapter);
        }
        // Only immutable counts/LOD identities are replayed. All pose, morph and slider inputs remain live.
        for (const auto& Integer : Cached->Integers)
            if (!Instance->SetIntVariable(Integer.Key, Integer.Value)) return false;
        for (int32 I = 0; I < Adapters.Num(); ++I)
        {
            const auto& Previous = Cached->Providers[I];
            (*Infos)[Previous.GraphIndex].ComputeGraphInstance.GetDataProviders()[Previous.ProviderIndex] = Adapters[I];
        }
        return !Adapters.IsEmpty();
    }
    return false;
}

void UEFClothingImmutableGraphProvider::RememberPrepared(UOptimusDeformerInstance* Instance,
    UOptimusDeformer* Graph, const UEFClothingSurfaceBinding* Binding, const FEFClothingSurfaceLODPairBinding& Pair)
{
    check(IsInGameThread());
    if (!CVarImmutableBindingBuffers.GetValueOnGameThread() || !CVarPreparedBinding.GetValueOnGameThread()
        || !Binding->BuildGuid.IsValid() || Binding->GarmentCompileFingerprint.IsEmpty()) return;
    auto* Infos = GetExecInfos(Instance);
    if (!Infos) return;
    auto Cached = MakeShared<FEFClothingPreparedGraphBinding, ESPMode::ThreadSafe>();
    Cached->Binding = Binding; Cached->Graph = Graph; Cached->BuildGuid = Binding->BuildGuid;
    Cached->CompileFingerprint = Binding->GarmentCompileFingerprint;
    Cached->GarmentLOD = Pair.GarmentTopology.LODIndex; Cached->BodyLOD = Pair.BodyTopology.LODIndex;
    Cached->GraphCount = Infos->Num();
    const TSet<FName> IntegerNames = {
        TEXT("EF_BindingVertexCount"), TEXT("EF_BodyVertexCount"), TEXT("EF_GarmentLODIndex"), TEXT("EF_BodyLODIndex"),
        TEXT("EF_NeighborReferenceCount"), TEXT("EF_WeldReferenceCount"), TEXT("EF_WitnessCount"), TEXT("EF_WitnessReferenceCount") };
    TArray<UEFClothingImmutableGraphProvider*> Adapters;
    for (int32 G = 0; G < Infos->Num(); ++G)
    {
        auto& Info = (*Infos)[G];
        if (!Info.ComputeGraph) continue;
        auto& Providers = Info.ComputeGraphInstance.GetDataProviders();
        for (int32 P = 0; P < Providers.Num(); ++P)
        {
            if (Providers[P] && Providers[P]->GetClass()->GetFName() == TEXT("OptimusGraphDataProvider")) return;
            auto* Adapter = Cast<UEFClothingImmutableGraphProvider>(Providers[P]);
            if (!Adapter) continue;
            auto& Previous = Cached->Providers.AddDefaulted_GetRef();
            Previous.GraphIndex = G; Previous.ProviderIndex = P; Previous.ComputeGraph = Info.ComputeGraph;
            Previous.Interface = GetInterface(Info.ComputeGraph, P);
            Previous.ParameterBytes = Adapter->ParameterBytes; Previous.Variables = Adapter->Variables;
            Previous.Buffers = Adapter->Buffers;
            for (const auto& Variable : Adapter->Variables)
            {
                if (!IntegerNames.Contains(Variable.Id.Name)) continue;
                const auto& Value = Instance->GetShaderValue(Variable.Id);
                if (Value.ShaderValue.Num() != sizeof(int32) || !Value.ArrayList.IsEmpty()) return;
                int32 Integer; FMemory::Memcpy(&Integer, Value.ShaderValue.GetData(), sizeof(Integer));
                Cached->Integers.Add(Variable.Id.Name, Integer);
            }
            Adapters.Add(Adapter);
        }
    }
    if (Adapters.IsEmpty() || Cached->Integers.Num() != IntegerNames.Num()) return;
    PreparedLookup.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
    if (PreparedLookup.Num() >= 128) return;
    for (auto* Adapter : Adapters) Adapter->Prepared = Cached;
    PreparedLookup.Add(Cached);
}

FComputeDataProviderRenderProxy* UEFClothingImmutableGraphProvider::GetRenderProxy()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(EFClothing_CopyGraphParameters);
    auto* Proxy = new FImmutableProxy;
    if (!Instance.IsValid() || !Buffers) return Proxy;
    Proxy->Parameters.SetNumZeroed(ParameterBytes);
    for (const auto& Variable : Variables)
    {
        const auto& Value = Instance->GetShaderValue(Variable.Id);
        if (Value.ShaderValue.Num() != Variable.Size) return Proxy; // no valid buffers: dispatch fallback
        if (Variable.Size) FMemory::Memcpy(Proxy->Parameters.GetData() + Variable.Offset, Value.ShaderValue.GetData(), Variable.Size);
    }
    Proxy->Buffers = Buffers;
    return Proxy;
}

void UEFClothingImmutableGraphProvider::Reset()
{
    Prepared.Reset(); Buffers.Reset(); Instance.Reset(); Variables.Reset();
}
