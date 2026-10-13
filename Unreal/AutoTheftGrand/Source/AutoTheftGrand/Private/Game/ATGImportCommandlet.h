// UnrealEditor-Cmd.exe AutoTheftGrand.uproject -run=ATGImport [-force]: turns the downloads in SourceAssets/
// into assets (ATGAssets::ImportAll), then makes any of the generated materials that are missing. Tools/import.sh
// runs it; Tools/package.sh runs it before cooking.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ATGImportCommandlet.generated.h"

UCLASS()
class UATGImportCommandlet : public UCommandlet {
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
