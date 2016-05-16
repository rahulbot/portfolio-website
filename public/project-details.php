<?php

$projectId = null;
if(array_key_exists("id",$_REQUEST) and is_numeric($_REQUEST['id'])){
	$projectId = $_REQUEST['id'];
} 

if($projectId==null){
	echo("Invalid Project Id");
	exit();
}

define("BASE","http://www.rahulbotics.com/personal-projects/");
if($projectId==12) {
	header( 'Location: '.BASE."boxdesigner" ) ;
	exit();
} else if($projectId==5) {
	header( 'Location: '.BASE."exif-o-matic" ) ;
	exit();
}

echo("Unknown Project id");
exit();

?>