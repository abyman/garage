#include <sys/types.h>
#include <sys/stat.h>
#include "mraa.h"
#include <stdio.h>
#include <unistd.h>
#include <mysql.h>
#include <syslog.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#define LOGFILE "/home/pi/work/mond.log"
#define PIDFILE "/home/pi/work/mond.pid"
#define DAEMON_NAME "mond"
#define MEDIAN_LEN 256

void sql_error(MYSQL *con);
MYSQL* sql_init(void);
int get_data(mraa_i2c_context dev, float *data);
mraa_i2c_context init_hih_i2c(void);
float median(int n, int x[]);
float mean(int m, int a[]);
int update_log(float T, float H);

int main(int argc, char *argv[]) { 
   //Set our Logging Mask and open the Log
   setlogmask(LOG_UPTO(LOG_ERR));
   openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
   syslog(LOG_INFO, "Entering Daemon");
   pid_t pid, sid;

   //Fork the Parent Process
   pid = fork();
   if (pid < 0) { exit(EXIT_FAILURE); }

   //We got a good pid, Close the Parent Process
   if (pid > 0) { exit(EXIT_SUCCESS); }

   //Change File Mask
   umask(0);

   //Create a new Signature Id for our child
   sid = setsid();
   if (sid < 0) { exit(EXIT_FAILURE); }

   //Change Directory
   //If we cant find the directory we exit with failure.
   if ((chdir("/")) < 0) { exit(EXIT_FAILURE); }

   /* Ensure only one copy */
   char *pidfile = PIDFILE;
   int pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);
   if (pidFilehandle == -1 )
   {
      /* Couldn't open lock file */
      syslog(LOG_ERR, "Could not open PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
   }

   /* Try to lock file */
   if (lockf(pidFilehandle,F_TLOCK,0) == -1)
   {
      /* Couldn't get lock on lock file */
      syslog(LOG_ERR, "Could not lock PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
   }
   /* Get and format PID */
   char str[10];
   sprintf(str,"%d\n",getpid());
   /* write pid to lockfile */
   write(pidFilehandle, str, strlen(str));
   syslog(LOG_INFO, "created pid file %s with content %s", pidfile, str);

   //Close Standard File Descriptors
   close(STDIN_FILENO);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);

   // Init Database if neccessary
   MYSQL *con = sql_init();
   // Init I2C connection to HIH
   mraa_i2c_context i2c = init_hih_i2c();
   float d[2];
   int mtemp[MEDIAN_LEN], mhumi[MEDIAN_LEN];
   int cnt = 0;
   float medTemp, medHumi;
   float medTempO = -1000.0;
   float medHumiO = -1000.0;
   char sql[100];
   
   while(1) {
      if( get_data(i2c, d) == -1 ) { 
         syslog(LOG_ERR, "Oh no, error with HIH");
      }
      else { 
         mtemp[cnt] = (int) 10.0*d[0]; 
         mhumi[cnt] = (int) 1.0*d[1]; 
      }
      if(cnt == (MEDIAN_LEN - 1)) {
         // Compute median and store to db
         medTemp = 0.1*median(MEDIAN_LEN,mtemp);
         medHumi = 1.0*median(MEDIAN_LEN,mhumi);
         if(medTemp != medTempO || medHumi != medHumiO) {
            sprintf(sql,"INSERT INTO data VALUES(CURRENT_DATE(),NOW(),%0.1f,%0.1f)",medTemp,medHumi);
            if (mysql_query(con, sql)) { sql_error(con); }
            syslog(LOG_INFO, "Adding %0.1f and %0.1f to dB (old values = %0.1f, %0.1f)",medTemp, medHumi, medTempO, medHumiO);
         }
         medTempO = medTemp; 
         medHumiO = medHumi;
      }
      // Update log file with current data
      if(update_log(d[0], d[1])) {
         syslog(LOG_ERR, " Oh no, could not open LOGFILE for writing");
      }
      sleep(10);
      cnt = (cnt + 1) % MEDIAN_LEN;
   }
   
	mraa_i2c_stop(i2c);
   mysql_close(con);
   closelog();
	return MRAA_SUCCESS;
}

void sql_error(MYSQL *con){
   syslog(LOG_ERR, "%s\n", mysql_error(con));
   mysql_close(con);
   exit(1);
}
MYSQL* sql_init(void) {
   MYSQL *con = mysql_init(NULL);

   if (con == NULL) 
   {
      syslog(LOG_ERR, "%s\n", mysql_error(con));
      exit(1);
   }

   // Connect to MySQL server as root
   if (mysql_real_connect(con, "localhost", "root", "beefcake", 
          NULL, 0, NULL, 0) == NULL) { sql_error(con); }
   // Create database if it does not exist
   if (mysql_query(con, "CREATE DATABASE IF NOT EXISTS tempdb")) { sql_error(con); }
   // delete user monni if it exist
   if (mysql_query(con, "SELECT 1 FROM mysql.user WHERE user = 'monni'")) { sql_error(con); }
   MYSQL_RES *result = mysql_store_result(con);
   if (mysql_fetch_row(result)) {
      if (mysql_query(con, "DROP USER monni@localhost")) { sql_error(con); }
   }
   // Create user monni 
   if (mysql_query(con, "CREATE USER monni@localhost IDENTIFIED BY 'm0nn1*'")) { sql_error(con); }
   // give monni some priviliges
   if (mysql_query(con, "GRANT ALL ON tempdb.* to monni@localhost")) { sql_error(con); }
   // Check if table exists
   if (mysql_query(con, "USE tempdb")) { sql_error(con); }
   if (mysql_query(con, "SHOW TABLES LIKE 'data'")) { sql_error(con); }
   result = mysql_store_result(con);
   if (mysql_fetch_row(result) == 0) {
      if (mysql_query(con, "CREATE TABLE data(tdate DATE, ttime TIME, temperature DOUBLE, humidity DOUBLE)")) { sql_error(con); }
   }
   mysql_free_result(result);
   mysql_close(con);
   con = mysql_init(NULL);
   // Connect to MySQL server as monni
   if (mysql_real_connect(con, "localhost", "monni", "m0nn1*", 
          "tempdb", 0, NULL, 0) == NULL) { sql_error(con); }
   return con;
}

mraa_i2c_context init_hih_i2c(void) {
   mraa_i2c_context i2c;
	i2c = mraa_i2c_init(0);
	mraa_i2c_address(i2c, 0x27);
   return i2c;
}

int get_data(mraa_i2c_context dev, float *data) {
   unsigned int stat = 1;
   unsigned int data16 = 0;
	uint8_t buf[4], hmsb,hlsb,tmsb,tlsb;
   // Measurement request
   mraa_i2c_write_byte(dev, (0x27 << 1));
   stat = 1;
   // Read Measurement
   while( stat == 1 ) {
      mraa_i2c_read_bytes_data(dev,((0x27 << 1) | 1),buf,4);
      hmsb= buf[0];
      hlsb= buf[1];
      tmsb= buf[2];
      tlsb= buf[3];
      stat = (unsigned int) (hmsb >> 6);
      //printf(" hmsb 0x%x \t hlsb 0x%x \t tmsb 0x%x \t tlsb 0x%x stat %d \n", hmsb,hlsb,tmsb,tlsb,stat);
      if(stat > 1) { 
         syslog(LOG_ERR, "   Status error, stat = %d\n", stat);
         break;
      }
   }
   if (stat == 0) { // New data
      // Compute Temperature
      data16 = ((unsigned int) tmsb << 6) | (unsigned int) (tlsb >> 2);
      data[0] = (float)(-40.0 + (165.0 * data16 / (float)16382));
      // printf("Temperature %f C \n",temp);
      // Compute Humidity
      data16 = ((unsigned int) (hmsb & 0x3F) << 8) | (unsigned int) hlsb;
      data[1] = (float)(100.0 * data16 / (float)16382 );
      return 0;
   }
   else
      return -1;
}

float median(int n, int x[]) {
   float temp;
   int i, j;
   // the following two loops sort the array x in ascending order
   for(i=0; i<n-1; i++) {
      for(j=i+1; j<n; j++) {
         if(x[j] < x[i]) {
            // swap elements
            temp = x[i];
            x[i] = x[j];
            x[j] = temp;
         }
      }
   }
   if(n%2==0) {
      // if there is an even number of elements, return mean of the two elements in the middle
      return((x[n/2] + x[n/2 - 1]) / 2.0);
   } else {
      // else return the element in the middle
      return x[n/2];
   }
}
float mean(int m, int a[]) {
   int sum=0, i;
   for(i=0; i<m; i++)
      sum+=a[i];
   return((float)sum/m);
}


int update_log(float T, float H) {
   FILE *f = fopen(LOGFILE, "w");
   if (f == NULL)
       return(1);
   fprintf(f, "%0.1f\n%0.1f\n", T, H);
   fclose(f);
   return 0;
}
