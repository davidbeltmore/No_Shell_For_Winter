#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolsetImage.h"
#include "EFCalystoDirectorToolset.generated.h"

/** Bounded NoShellForWinter Calysto context, traversal arming and diagnostics. */
UCLASS()
class UEFCalystoDirectorToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:
	/** Read actual project, engine, Editor/PIE worlds and dirty content/map packages. */
	UFUNCTION(meta = (AICallable))
	static FString ReadEditorContext();
	/** Arm the fixed 150-second traversal script with temporary background-throttle relief; does not start PIE. EvidenceName is 1..80 ASCII letters/digits/_/-. Mode is native_parity or full. Require returned ARMED receipt before StartPIE. */
	UFUNCTION(meta = (AICallable))
	static FString ArmTraversal(const FString& EvidenceName, const FString& Mode);
	/** Read the existing Director on the sole actual PIE GameInstance, without creating a run. */
	UFUNCTION(meta = (AICallable))
	static FString ReadDirectorDiagnostics();
	/** Run the fixed read-only V6-to-direct-model staging importer with fresh archived evidence. SaveMaster requires complete mappings and saves only the exact unversioned master through Editor APIs. Never switches runtime authority. EvidenceName: 1..80 ASCII letters/digits/_/-. Requires stopped PIE and no dirty packages. */
	UFUNCTION(meta = (AICallable))
	static FString MigrateAuthoring(const FString& EvidenceName, bool SaveMaster);
	/** Capture only the already-open master asset's native Details window at its actual resolution. Does not open, expand, edit or save anything. Stopped PIE required. */
	UFUNCTION(meta = (AICallable))
	static FToolsetImage CaptureAuthoringDetails();
	/** Request deferred graceful Editor closure only with no dirty packages or active/queued PIE; rechecks before closing. This is not process-exit evidence. */
	UFUNCTION(meta = (AICallable))
	static FString RequestEditorShutdown();
	/** Module shutdown removes owned tickers/delegates and restores temporary traversal performance settings. */
	static void CancelPendingShutdown();
};
