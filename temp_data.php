<?php
   $IV = $_GET['a'];
   // Make a MySQL Connection
   mysql_connect("localhost", "bro", "wser") or die(mysql_error());
   mysql_select_db("tempdb") or die(mysql_error());

//   $qu = "SELECT * FROM data WHERE tdate > NOW() - INTERVAL " . "1" . " WEEK"
   // Get all the data from the "example" table
   $result = mysql_query("SELECT * FROM data WHERE tdate > NOW() - INTERVAL " . $IV . " WEEK") or die(mysql_error());  

   while ($row = mysql_fetch_array($result)) {
      echo $row['tdate'] . "\t" . $row['ttime'] . "\t" . $row['temperature'] . "\t" . $row['humidity'] . "\n";
   }
?> 
