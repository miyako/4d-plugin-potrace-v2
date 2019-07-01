//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"loxie-orig.png"
READ PICTURE FILE:C678($path;$png)
PICTURE TO BLOB:C692($png;$bmp;".bmp")

$option:=New object:C1471
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"loxie-raw.svg"
WRITE PICTURE FILE:C680($path;$t.image)

$option:=New object:C1471("scale";2;"filter";2;"threshold";0.48)
$pgm:=Mkbitmap ($bmp;$option)  //-f 2 -s 2 -t 0.48

$option:=New object:C1471("format";".svg";"turdsize";5)
$t:=Potrace ($pgm;$option)  // -t 5

$path:=System folder:C487(Desktop:K41:16)+"loxie.svg"
WRITE PICTURE FILE:C680($path;$t.image)
