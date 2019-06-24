//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"test.png"
READ PICTURE FILE:C678($path;$png)
PICTURE TO BLOB:C692($png;$bmp;".bmp")

$t:=Potrace ($bmp)

$image:=$t.image

SET PICTURE TO PASTEBOARD:C521($image)
