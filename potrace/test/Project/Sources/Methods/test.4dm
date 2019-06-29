//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"test.png"
READ PICTURE FILE:C678($path;$png)
PICTURE TO BLOB:C692($png;$bmp;".bmp")

$option:=New object:C1471("format";".svg")
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"test.svg"
WRITE PICTURE FILE:C680($path;$t.image)

$option:=New object:C1471("format";".pdf")
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"test.pdf"
WRITE PICTURE FILE:C680($path;$t.image)