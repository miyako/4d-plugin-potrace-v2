//%attributes = {}
  //process templates/infoPlist.strings

$params:=New object:C1471
$params.PRODUCT_NAME:="Potrace"
$params.PRODUCT_VERSION:="2.0"
$params.AUTHOR:="miyako"
$params.COPYRIGHT_YEAR:=Year of:C25(Current date:C33)

$path:=Path to object:C1547(Get 4D folder:C485(Database folder:K5:14))
$templatesFolder:=$path.parentFolder+"templates"+Folder separator:K24:12
$infoPlistPath:=$templatesFolder+"InfoPlist.strings"
$infoPlist:=Document to text:C1236($infoPlistPath;"utf-8")
PROCESS 4D TAGS:C816($infoPlist;$infoPlist;$params)
$infoPlistPath:=$path.parentFolder+"English.lproj"+Folder separator:K24:12+"InfoPlist.strings"
TEXT TO DOCUMENT:C1237($infoPlistPath;$infoPlist;"utf-8")