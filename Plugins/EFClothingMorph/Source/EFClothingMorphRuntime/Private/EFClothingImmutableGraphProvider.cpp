#include "EFClothingImmutableGraphProvider.h"
#include "ComputeFramework/ComputeGraph.h"
#include "HAL/IConsoleManager.h"
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

namespace
{
using FBuffers = FEFClothingImmutableGraphBuffers;
using FBuffersPtr = TSharedPtr<FBuffers, ESPMode::ThreadSafe>;

// Weak lookup only: owners and in-flight proxies determine lifetime, never the cache.
// Limit the lookup even if many different outfits are simultaneously alive.
TMap<FString, TWeakPtr<FBuffers, ESPMode::ThreadSafe>> BufferLookup;

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

bool Prepare(UEFClothingImmutableGraphProvider* Adapter, UObject* Source, UObject* Interface, FString& Failure)
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

FComputeDataProviderRenderProxy* UEFClothingImmutableGraphProvider::GetRenderProxy()
{
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
    Buffers.Reset(); Instance.Reset(); Variables.Reset();
}
