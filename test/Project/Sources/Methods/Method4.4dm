//%attributes = {}
  //process templates/v9/project.xcodeproj/project.pbxproj

$params:=New object:C1471
$params.PRODUCT_NAME:="Potrace"

$path:=Path to object:C1547(Get 4D folder:C485(Database folder:K5:14))
$templatesFolder:=$path.parentFolder+"templates"+Folder separator:K24:12

If (True:C214)
	$projectPath:=$templatesFolder+"v9"+Folder separator:K24:12+"project.xcodeproj"+Folder separator:K24:12+"project.pbxproj"
	$project:=Document to text:C1236($projectPath;"utf-8")
	PROCESS 4D TAGS:C816($project;$project;$params)
	$projectPath:=$path.parentFolder+$params.PRODUCT_NAME+".v9.xcodeproj"
	  //if document, remove it
	If (Test path name:C476($projectPath)=Is a document:K24:1)
		DELETE DOCUMENT:C159($projectPath)
	End if 
	$projectPath:=$projectPath+Folder separator:K24:12
	  //if folder, remove it
	DELETE FOLDER:C693($projectPath;Delete with contents:K24:24)
	CREATE FOLDER:C475($projectPath;*)
	$projectPath:=$projectPath+"project.pbxproj"
	  //no BOM, please!
	CONVERT FROM TEXT:C1011($project;"utf-8";$projectData)
	BLOB TO DOCUMENT:C526($projectPath;$projectData)
End if 

If (True:C214)
	$projectPath:=$templatesFolder+"v10"+Folder separator:K24:12+"project.xcodeproj"+Folder separator:K24:12+"project.pbxproj"
	$project:=Document to text:C1236($projectPath;"utf-8")
	PROCESS 4D TAGS:C816($project;$project;$params)
	$projectPath:=$path.parentFolder+$params.PRODUCT_NAME+".xcodeproj"
	  //if document, remove it
	If (Test path name:C476($projectPath)=Is a document:K24:1)
		DELETE DOCUMENT:C159($projectPath)
	End if 
	$projectPath:=$projectPath+Folder separator:K24:12
	  //if folder, remove it
	DELETE FOLDER:C693($projectPath;Delete with contents:K24:24)
	CREATE FOLDER:C475($projectPath;*)
	$projectPath:=$projectPath+"project.pbxproj"
	  //no BOM, please!
	CONVERT FROM TEXT:C1011($project;"utf-8";$projectData)
	BLOB TO DOCUMENT:C526($projectPath;$projectData)
End if 

  //create target 
$targetPath:=$path.parentFolder+\
"test"+Folder separator:K24:12+"Plugins"+Folder separator:K24:12+\
$params.PRODUCT_NAME+".bundle"+Folder separator:K24:12+"Contents"+Folder separator:K24:12
CREATE FOLDER:C475($targetPath;*)

CREATE FOLDER:C475($targetPath+"MacOS"+Folder separator:K24:12;*)
CREATE FOLDER:C475($targetPath+"QuickLook"+Folder separator:K24:12;*)
CREATE FOLDER:C475($targetPath+"Resources"+Folder separator:K24:12;*)
CREATE FOLDER:C475($targetPath+"Windows"+Folder separator:K24:12;*)
CREATE FOLDER:C475($targetPath+"Windows64"+Folder separator:K24:12;*)

  //stubs
$stubPath:=$targetPath+"Windows"+Folder separator:K24:12+$params.PRODUCT_NAME+".4DX"
If (Test path name:C476($stubPath)#Is a document:K24:1)
	COPY DOCUMENT:C541($templatesFolder+"Windows"+Folder separator:K24:12+"plugin.4DX";$stubPath)
End if 

$stubPath:=$targetPath+"Windows64"+Folder separator:K24:12+$params.PRODUCT_NAME+".4DX"
If (Test path name:C476($stubPath)#Is a document:K24:1)
	COPY DOCUMENT:C541($templatesFolder+"Windows64"+Folder separator:K24:12+"plugin.4DX";$stubPath)
End if 

$stubPath:=$targetPath+"MacOS"+Folder separator:K24:12+$params.PRODUCT_NAME
If (Test path name:C476($stubPath)#Is a document:K24:1)
	COPY DOCUMENT:C541($templatesFolder+"MacOS"+Folder separator:K24:12+"plugin";$stubPath)
End if 

  //icon
$iconPath:=$targetPath+"QuickLook"+Folder separator:K24:12+"Thumbnail.png"
If (Test path name:C476($iconPath)#Is a document:K24:1)
	COPY DOCUMENT:C541($templatesFolder+"Thumbnail.png";$iconPath)
End if 

  //constants.xlf
$projectFolder:=$path.parentFolder
$constantsXlfPath:=$projectFolder+"constants.xlf"

$constantsPath:=$targetPath+"Resources"+Folder separator:K24:12+"constants.xlf"
COPY DOCUMENT:C541($constantsXlfPath;$constantsPath;*)

  //manifest.json
$projectFolder:=$path.parentFolder
$manifestJsonPath:=$projectFolder+"manifest.json"

$manifestPath:=$targetPath+"manifest.json"
COPY DOCUMENT:C541($manifestJsonPath;$manifestPath;*)
