//%attributes = {}
  //sample constants.json

C_OBJECT:C1216($constants)
$constants:=New object:C1471

$constants.themes:=New collection:C1472
$constants.themes[0]:=New object:C1471
$constants.themes[0].theme:="Potrace Backend"
$constants.themes[0].constants:=New collection:C1472

$constants.themes[0].constants[0]:=New object:C1471
$constants.themes[0].constants[0].name:="POTRACE_BACKEND"
$constants.themes[0].constants[0].value:="backend:S"

$constants.themes[0].constants[1]:=New object:C1471
$constants.themes[0].constants[1].name:="POTRACE_BACKEND_SVG"
$constants.themes[0].constants[1].value:="svg:S"

$constants.themes[0].constants[2]:=New object:C1471
$constants.themes[0].constants[2].name:="POTRACE_BACKEND_PDF"
$constants.themes[0].constants[2].value:="pdf:S"

$constants.themes[0].constants[3]:=New object:C1471
$constants.themes[0].constants[3].name:="POTRACE_BACKEND_EPS"
$constants.themes[0].constants[3].value:="eps:S"

$constants.themes[0].constants[4]:=New object:C1471
$constants.themes[0].constants[4].name:="POTRACE_BACKEND_PS"
$constants.themes[0].constants[4].value:="ps:S"

$constants.themes[0].constants[5]:=New object:C1471
$constants.themes[0].constants[5].name:="POTRACE_BACKEND_PDFPAGE"
$constants.themes[0].constants[5].value:="pdfpage:S"

$constants.themes[0].constants[6]:=New object:C1471
$constants.themes[0].constants[6].name:="POTRACE_BACKEND_PGM"
$constants.themes[0].constants[6].value:="pgm:S"

$constants.themes[0].constants[7]:=New object:C1471
$constants.themes[0].constants[7].name:="POTRACE_BACKEND_DXF"
$constants.themes[0].constants[7].value:="dxf:S"

$constants.themes[0].constants[8]:=New object:C1471
$constants.themes[0].constants[8].name:="POTRACE_BACKEND_GEOJSON"
$constants.themes[0].constants[8].value:="geojson:S"

$constants.themes[0].constants[9]:=New object:C1471
$constants.themes[0].constants[9].name:="POTRACE_BACKEND_GIMPPATH"
$constants.themes[0].constants[9].value:="gimppath:S"

$constants.themes[0].constants[10]:=New object:C1471
$constants.themes[0].constants[10].name:="POTRACE_BACKEND_XFIG"
$constants.themes[0].constants[10].value:="xfig:S"

$constants.themes[1]:=New object:C1471
$constants.themes[1].theme:="mkbitmap"
$constants.themes[1].constants:=New collection:C1472

$json:=JSON Stringify:C1217($constants;*)

$path:=Path to object:C1547(Get 4D folder:C485(Database folder:K5:14))
$projectFolder:=$path.parentFolder
$constantsJsonPath:=$projectFolder+"constants.json"

TEXT TO DOCUMENT:C1237($constantsJsonPath;$json;"utf-8")