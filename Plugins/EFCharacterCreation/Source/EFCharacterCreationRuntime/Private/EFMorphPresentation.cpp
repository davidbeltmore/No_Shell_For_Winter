#include "EFMorphPresentation.h"

namespace EFMorphPresentation
{
	const TArray<FString>& Sections(FName Category)
	{
		static const TArray<FString> Head = { TEXT("General"), TEXT("Skull"), TEXT("Forehead"), TEXT("Brow"), TEXT("Eyes"), TEXT("Eyelids"), TEXT("Eyelashes"), TEXT("Ears"), TEXT("Nose"), TEXT("Cheeks"), TEXT("Mouth & Lips"), TEXT("Jaw & Chin"), TEXT("Tongue & Teeth"), TEXT("Expressions") };
		static const TArray<FString> Body = { TEXT("General & Proportions"), TEXT("Neck"), TEXT("Shoulders"), TEXT("Chest"), TEXT("Breasts & Nipples"), TEXT("Back"), TEXT("Arms"), TEXT("Hands & Nails"), TEXT("Abdomen & Waist"), TEXT("Hips & Glutes"), TEXT("Legs"), TEXT("Feet"), TEXT("Genitals") };
		return Category == TEXT("Head") ? Head : Body;
	}

	FString NormalizeSection(const FString& Section)
	{
		static const TMap<FString, FString> Aliases = {
			{TEXT("Face"), TEXT("General")}, {TEXT("Brows"), TEXT("Brow")}, {TEXT("Cheek"), TEXT("Cheeks")}, {TEXT("Cheekbone"), TEXT("Cheeks")},
			{TEXT("Cranium"), TEXT("Skull")}, {TEXT("Eye"), TEXT("Eyes")}, {TEXT("Ear"), TEXT("Ears")},
			{TEXT("Earlobe"), TEXT("Ears")}, {TEXT("Eyelid"), TEXT("Eyelids")}, {TEXT("Jaw"), TEXT("Jaw & Chin")},
			{TEXT("Chin"), TEXT("Jaw & Chin")}, {TEXT("Mouth"), TEXT("Mouth & Lips")}, {TEXT("Lip"), TEXT("Mouth & Lips")}
		};
		if (const FString* Match = Aliases.Find(Section)) return *Match;
		return Section;
	}

	FClassification Classify(const FString& Name)
	{
		FString Normalized;
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			const TCHAR C = Name[Index];
			if (Index > 0 && FChar::IsUpper(C) && FChar::IsLower(Name[Index - 1])) Normalized += TEXT(' ');
			Normalized += FChar::IsAlnum(C) ? FChar::ToLower(C) : TEXT(' ');
		}
		TArray<FString> Words;
		Normalized.ParseIntoArrayWS(Words);
		auto Has = [&Words](std::initializer_list<const TCHAR*> Tokens)
		{
			for (const TCHAR* Token : Tokens) if (Words.Contains(Token)) return true;
			return false;
		};
		auto Result = [](const TCHAR* Category, const TCHAR* Section) { return FClassification{ FName(Category), FString(Section), true }; };
		// Whole expressions take precedence over the body part mentioned by their name.
		static const TSet<FString> Expressions = { TEXT("wink"), TEXT("triumph"), TEXT("tired"), TEXT("suspicious"), TEXT("surprised"), TEXT("snarl"), TEXT("silly"), TEXT("shock"), TEXT("serious"), TEXT("scream"), TEXT("sarcastic"), TEXT("sad"), TEXT("rage"), TEXT("pouty"), TEXT("pleased"), TEXT("pain"), TEXT("irritated"), TEXT("incredulous"), TEXT("ill"), TEXT("ignore"), TEXT("happy"), TEXT("glare"), TEXT("frown"), TEXT("flirting"), TEXT("fierce"), TEXT("fear"), TEXT("excitement"), TEXT("drunk"), TEXT("disgust"), TEXT("desire"), TEXT("contempt"), TEXT("confused"), TEXT("confident"), TEXT("concentrate"), TEXT("bored"), TEXT("bereft"), TEXT("angry"), TEXT("afraid") };
		if (Name.StartsWith(TEXT("Base Anime ")) || Name.StartsWith(TEXT("Smile ")) || Expressions.Contains(Name.ToLower())) return Result(TEXT("Head"), TEXT("Expressions"));
		if (Has({TEXT("gpl"), TEXT("gpc"), TEXT("gpv"), TEXT("dk"), TEXT("genitals"), TEXT("groin")})) return Result(TEXT("Body"), TEXT("Genitals"));
		// Prefix first: Skull Above Ear Width belongs to Skull, not Ears.
		const FString First = Words.IsEmpty() ? FString() : Words[0];
		if (First == TEXT("skull") || First == TEXT("cranium")) return Result(TEXT("Head"), TEXT("Skull"));
		if (First == TEXT("forehead")) return Result(TEXT("Head"), TEXT("Forehead"));
		if (Has({TEXT("eyelashes"), TEXT("eyelash"), TEXT("lashes"), TEXT("lash")})) return Result(TEXT("Head"), TEXT("Eyelashes"));
		if (Has({TEXT("eyelid"), TEXT("eyelids")})) return Result(TEXT("Head"), TEXT("Eyelids"));
		if (Has({TEXT("brow"), TEXT("brows")})) return Result(TEXT("Head"), TEXT("Brow"));
		if (Has({TEXT("eye"), TEXT("eyes"), TEXT("eyeball")})) return Result(TEXT("Head"), TEXT("Eyes"));
		if (Has({TEXT("ear"), TEXT("ears"), TEXT("earlobe")})) return Result(TEXT("Head"), TEXT("Ears"));
		if (Has({TEXT("nose"), TEXT("nostril")})) return Result(TEXT("Head"), TEXT("Nose"));
		if (Has({TEXT("cheek"), TEXT("cheeks"), TEXT("cheekbone")})) return Result(TEXT("Head"), TEXT("Cheeks"));
		if (Has({TEXT("jaw"), TEXT("chin")})) return Result(TEXT("Head"), TEXT("Jaw & Chin"));
		if (Has({TEXT("tongue"), TEXT("teeth"), TEXT("tooth")})) return Result(TEXT("Head"), TEXT("Tongue & Teeth"));
		if (Has({TEXT("mouth"), TEXT("lip"), TEXT("lips")})) return Result(TEXT("Head"), TEXT("Mouth & Lips"));
		if (Has({TEXT("head"), TEXT("face"), TEXT("beard")})) return Result(TEXT("Head"), TEXT("General"));
		if (Has({TEXT("breast"), TEXT("breasts"), TEXT("nipple"), TEXT("nipples")})) return Result(TEXT("Body"), TEXT("Breasts & Nipples"));
		if (Has({TEXT("neck")})) return Result(TEXT("Body"), TEXT("Neck"));
		if (Has({TEXT("shoulder"), TEXT("shoulders"), TEXT("collarbone"), TEXT("clavicle"), TEXT("traps")})) return Result(TEXT("Body"), TEXT("Shoulders"));
		if (Has({TEXT("chest"), TEXT("pectoral"), TEXT("pectorals"), TEXT("pecmovement"), TEXT("sternum"), TEXT("ribcage")})) return Result(TEXT("Body"), TEXT("Chest"));
		if (Has({TEXT("hip"), TEXT("hips"), TEXT("glute"), TEXT("glutes"), TEXT("butt"), TEXT("pelvis")})) return Result(TEXT("Body"), TEXT("Hips & Glutes"));
		if (Has({TEXT("back"), TEXT("scapula"), TEXT("lats")})) return Result(TEXT("Body"), TEXT("Back"));
		if (Has({TEXT("arm"), TEXT("arms"), TEXT("forearm"), TEXT("forearms"), TEXT("upperarm"), TEXT("bicep"), TEXT("tricep")})) return Result(TEXT("Body"), TEXT("Arms"));
		if (Has({TEXT("hand"), TEXT("hands"), TEXT("nail"), TEXT("nails"), TEXT("finger")})) return Result(TEXT("Body"), TEXT("Hands & Nails"));
		if (Has({TEXT("stomach"), TEXT("abdominals"), TEXT("abdomen"), TEXT("waist"), TEXT("navel"), TEXT("pregnant")})) return Result(TEXT("Body"), TEXT("Abdomen & Waist"));
		if (Has({TEXT("foot"), TEXT("feet"), TEXT("toe"), TEXT("toes"), TEXT("ankle"), TEXT("ankles")})) return Result(TEXT("Body"), TEXT("Feet"));
		if (Has({TEXT("thigh"), TEXT("thighs"), TEXT("knee"), TEXT("knees"), TEXT("leg"), TEXT("legs"), TEXT("calf"), TEXT("calves"), TEXT("shin"), TEXT("shins")})) return Result(TEXT("Body"), TEXT("Legs"));
		if (Has({TEXT("body"), TEXT("proportion"), TEXT("torso")})) return Result(TEXT("Body"), TEXT("General & Proportions"));
		return FClassification{};
	}

	FString CleanLabel(const FString& Name, const FString& Section)
	{
		FString Label = Name;
		Label.ReplaceInline(TEXT("_"), TEXT(" "));
		for (const TCHAR* Prefix : {TEXT("Base Anime "), TEXT("GPL "), TEXT("GPC "), TEXT("GPV "), TEXT("DK ")}) Label.RemoveFromStart(Prefix);
		// Only remove an anatomical prefix when its context is unambiguous.
		if (Section == TEXT("Brow")) Label.RemoveFromStart(TEXT("Brow "));
		if (Section == TEXT("Nose")) Label.RemoveFromStart(TEXT("Nose "));
		if (Section == TEXT("Eyelids")) Label.RemoveFromStart(TEXT("Eyelid "));
		Label.TrimStartAndEndInline();
		return Label.IsEmpty() ? Name : Label;
	}
}
