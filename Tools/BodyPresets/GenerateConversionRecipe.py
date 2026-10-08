"""Generate the reviewed G9-only recipe from the installed DAZ converter.

This reads the vendor file; it never edits it. Review its diff after a plugin update.
The runtime converter does not depend on the DAZ module or invoke its modal commit.
"""
import hashlib
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = Path('D:/Unreal Engine 5/Library/UE_5.8/Engine/Plugins/DazToUnreal/Source/DazToUnreal/Private/DazToUnrealBlueprintUtils.cpp')
data = source.read_bytes()
text = data.decode('utf-8-sig')
body = text.split('// G9 Renaming ')[1].split('Modifier->CommitSkeletonToSkeletalMesh();')[0]
body = body[body.index('// Rename Spine'):]
body = '\n'.join(line for line in body.splitlines() if not line.strip().startswith('//'))
body = re.sub(r'\n\s*\n', '\n', body)
# The enclosing conversion checked the exact G9 inputs. Every modifier operation
# must succeed here, unlike the vendor's multi-generation best-effort recipe.
body = re.sub(r'(Modifier->(?:RenameBone|SetBoneTransform)\([^;]+\));', r'if (!\1) return false;', body)
body = body.replace('FEulerTransform(LFootTransform).GetRotation().Rotator()', 'LFootTransform.GetRotation().Rotator()')
body = body.replace('FEulerTransform(RFootTransform).GetRotation().Rotator()', 'RFootTransform.GetRotation().Rotator()')
out = root/'Plugins/EFCharacterCreation/Source/EFCharacterCreationEditor/Private/EFGenesis9ConversionRecipe.inl'
header = '// Generated from the installed DazToUnreal G9 conversion operations.\n'
header += '// Source SHA256: '+hashlib.sha256(data).hexdigest()+'\n'
header += '// Regenerate/review with Tools/BodyPresets/GenerateConversionRecipe.py.\n'
header += '// No shared skeleton assignment, asset saving or vendor physics rebuild.\n'
out.write_text(header+'static bool ApplyGenesis9Recipe(USkeletonModifier* Modifier)\n{\n'
    +'\tif (!Modifier->ParentBone("pelvis", "root") || !Modifier->ParentBone("hip", "pelvis")) return false;\n'
    +body+'\n\treturn true;\n}\n', encoding='utf-8')
print(out)
