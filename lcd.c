#include <sys/types.h>
#include <sys/stat.h>
#include "mraa.h"
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#define LOGFILE "/home/pi/work/mond.log"
#define PIDFILE "/home/pi/work/lcdd.pid"
#define DAEMON_NAME "lcdd"

#define LCD_E 21
#define LCD_RS 19
#define LCD_D4 23
#define LCD_D5 22
#define LCD_D6 26
#define LCD_D7 24

void pulseEnable(mraa_gpio_context *g);
void lcd_byte(mraa_gpio_context *g, char bits);
void SetCmdMode(mraa_gpio_context *g);
void SetChrMode(mraa_gpio_context *g);
void lcd_text(mraa_gpio_context *g, char *s, int line);
void lcd_init(mraa_gpio_context *g);
void lcd_close(mraa_gpio_context *g);

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

   // Ensure only one copy 
   char *pidfile = PIDFILE;
   int pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);
   if (pidFilehandle == -1 )
   {
      // Couldn't open lock file 
      syslog(LOG_ERR, "Could not open PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
   }

   // Try to lock file 
   if (lockf(pidFilehandle,F_TLOCK,0) == -1)
   {
      // Couldn't get lock on lock file 
      syslog(LOG_ERR, "Could not lock PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
   }
   // Get and format PID 
   char str[10];
   sprintf(str,"%d\n",getpid());
   // write pid to lockfile 
   write(pidFilehandle, str, strlen(str));
   syslog(LOG_INFO, "created pid file %s with content %s", pidfile, str);

   //Close Standard File Descriptors
   close(STDIN_FILENO);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);

   // Main code
   mraa_init();
   mraa_gpio_context gpio[6];
   char l1[16] = "LCD Daemon";
   char l2[16] = " Started ";
   char T[5], H[5];
   int numReads = 0;
   time_t now = time(NULL);
   struct tm *t = localtime(&now);
   lcd_init(gpio);
   lcd_text(gpio, l1, 1);
   lcd_text(gpio, l2, 2);
   sleep(1);
   while(1) {
      // Read LOGFILE to retrieve Temp and Humidity
      FILE *f = fopen(LOGFILE, "r");
      if (f == NULL)
         return(1);
      numReads = fscanf(f, "%s", T);
      numReads += fscanf(f, "%s", H);
      fclose(f);
      if(numReads > 1) {
         sprintf(l1, "  %sC  %s%%", T, H);
         lcd_text(gpio, l1, 1);
      }
      // Grab and display the date and time on the second line
      time(&now);
      t = localtime(&now);
      strftime(l2, sizeof(l2), " %d.%m.%y %H:%M", t);
      lcd_text(gpio, l2, 2); 
      // Sleep for a minute
      sleep(58);
   }
   
   lcd_close(gpio);
	return MRAA_SUCCESS;
}

// Functions
void pulseEnable (mraa_gpio_context *g)
{
   mraa_gpio_write(g[0], 1) ; // LCD_E
   usleep(500); //  1/2 millisecond pause - enable pulse must be > 450us
   mraa_gpio_write(g[0], 0) ;
}

/*
send a byte to the lcd in two nibbles
before calling use SetChrMode or SetCmdMode to determine whether to send character or command
*/
void lcd_byte(mraa_gpio_context *g, char bits)
{
   mraa_gpio_write(g[2],(bits & 0x10)) ; 
   mraa_gpio_write(g[3],(bits & 0x20)) ; 
   mraa_gpio_write(g[4],(bits & 0x40)) ; 
   mraa_gpio_write(g[5],(bits & 0x80)) ; 
   pulseEnable(g);

   mraa_gpio_write(g[2],(bits & 0x1)) ; 
   mraa_gpio_write(g[3],(bits & 0x2)) ; 
   mraa_gpio_write(g[4],(bits & 0x4)) ; 
   mraa_gpio_write(g[5],(bits & 0x8)) ; 
   pulseEnable(g);         
}

void SetCmdMode(mraa_gpio_context *g)
{
   mraa_gpio_write(g[1], 0); // set for commands
}

void SetChrMode(mraa_gpio_context *g)
{
   mraa_gpio_write(g[1], 1); // set for characters
}

void lcd_text(mraa_gpio_context *g, char *s, int line)
{
   SetCmdMode(g); // set to cmd mode to send line cmd
   if(line == 1) 
      lcd_byte(g, 0x80); // line 1
   else
      lcd_byte(g, 0xC0); // line 2
   SetChrMode(g); // set to char mode to send text
   while(*s) 
      lcd_byte(g, *s++);
}

void lcd_init(mraa_gpio_context *g)
{
   // set up pi pins for output
   g[0] = mraa_gpio_init(LCD_E);
   g[1] = mraa_gpio_init(LCD_RS);
   g[2] = mraa_gpio_init(LCD_D4);
   g[3] = mraa_gpio_init(LCD_D5);
   g[4] = mraa_gpio_init(LCD_D6);
   g[5] = mraa_gpio_init(LCD_D7);
   for(int i = 0; i < 6; i++) { 
      mraa_gpio_dir(g[i], MRAA_GPIO_OUT);
   }
   // initialise LCD
   SetCmdMode(g); // set for commands
   lcd_byte(g,0x33); // 2 line mode
   lcd_byte(g,0x32); // 2 line mode
   lcd_byte(g,0x06); // 2 line mode
   lcd_byte(g,0x0C); // 2 line mode
   lcd_byte(g,0x28); // display on, cursor off, blink off
   lcd_byte(g,0x01);  // clear screen
   usleep(3000);       // clear screen is slow!
   SetChrMode(g);
}

void lcd_close(mraa_gpio_context *g)
{
   for(int i = 0; i < 6; i++)
      mraa_gpio_close(g[i]);
}
