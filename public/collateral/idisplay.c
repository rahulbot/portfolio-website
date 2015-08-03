/************************************************************************
 *
 * Fetches news from CNN or BBC and scrolls across VFD.
 *
 *	- add auto refetch dhcp if it goes down...
 *	- add webpage to choose from fixed list of servers
 *	- can't get stupid NIST time to set correctly!
 *
 *	2003.06.17 - added retry for DHCP on failure
 *	2003.06.16 - fixed init and prettied up printing
 *	2003.06.15 - adapted from display and news-ticker code
 *
 ************************************************************************/

/*******************************************************************
   Network Settings (DHCP)
 ******************************************************************/
#define USE_DHCP		// Main define to cause BOOTP or DHCP to be used.
#define DHCP_CLASS_ID	"Rabbit-TCPIP:Z-World:DHCP-Test:1.0.0"
#define DHCP_NUM_SMTP	1		// Get an SMTP server if possible
#define DHCP_MINRETRY	5		// Min DHCP retry interval (secs).  If undefined, defaults to 60.
#define MY_IP_ADDRESS	"10.10.6.100"
#define MY_NETMASK		"255.255.255.0"
#define MY_GATEWAY		"10.10.6.1"
#define HTTP_PORT		80

#memmap xmem
#use dcrtcp.lib

/*******************************************************************
   LCD Functions (2-line)
 ******************************************************************/
#define delay_us(count) asm ld a,count $ ld b,a $ nop $ nop $ db 0x10, -4

#define DISPLAY_E PEB0R
#define DATA_PORT PADR 
#define DATA_PORT_SHADOW PADRShadow

void dprint_init_ports() {
	//set PA0:PA7 as outputs, disable slave port
	WrPortI(SPCR, & SPCRShadow, 0x84);
	//set PE0 for output ()
	WrPortI(PEDDR, & PEDDRShadow, 0x01 | PEDDRShadow);
	WrPortI(PEFR, & PEFRShadow, 0x00);
	//write zero to start with...
	WrPortI(DATA_PORT, & DATA_PORT_SHADOW, 0x00);
	WrPortI(DISPLAY_E, NULL, 0x00);
}

//use low four
void dprint_write_nibble(char d,int instr){
	int i;
	char fixed_data;
	//write the nibble
	fixed_data = 0;
	if(instr==1) fixed_data = d & 0x0F;	//just the low four
	else fixed_data = d & 0x4F;
	WrPortI(DATA_PORT, & DATA_PORT_SHADOW, fixed_data);
	//toggle E
	WrPortI(DISPLAY_E, NULL, 0xFF);
	delay_us(20);
	WrPortI(DISPLAY_E, NULL, 0x00);
	delay_us(200);
	//printf("  wrote nibble 0x%02x (0x%02x)\n",d,fixed_data);
}

void dprint_write_byte(char d,int instr){
	int i;
	char fixed_data;
	//write the high 4
	fixed_data = d >> 4;
	if(instr==1) fixed_data = fixed_data;
	else fixed_data = fixed_data | 0x40;
	WrPortI(DATA_PORT, & DATA_PORT_SHADOW, fixed_data);
	//toggle E
	WrPortI(DISPLAY_E, NULL, 0xFF);
	delay_us(20);
	WrPortI(DISPLAY_E, NULL, 0x00);
	delay_us(20);
	//printf("  wrote byte 0x%02x (0x%02x",d,fixed_data);
	//write the low 4
	if(instr==1) fixed_data = d & 0x0F;
	else fixed_data = d | 0x40;
	WrPortI(DATA_PORT, & DATA_PORT_SHADOW, fixed_data);
	//toggle E
	WrPortI(DISPLAY_E, NULL, 0xFF);
	delay_us(20);
	WrPortI(DISPLAY_E, NULL, 0x00);
	//for some reason it doesn't work if I take this out :-(
	delay_us(200);
	for(i=0;i<100;i++);
}

void dprint_init(){
	delay_us(2000);			//wait 20ms
	dprint_write_nibble(0x03,1);
	delay_us(50);			//wait 5ms
	dprint_write_nibble(0x03,1);
	dprint_write_nibble(0x03,1);
	dprint_write_nibble(0x02,1);
	dprint_write_byte(0x28,1);	//interface length
	dprint_write_byte(0x04,1);	//display off
	dprint_write_byte(0x01,1);	//clear
	dprint_write_byte(0x06,1);	//cursor move direction
	dprint_write_byte(0x0C,1);	//enable display, underline cursor only
}

//clear the display
void dprint_clear(){
	dprint_write_byte(0x01,1);	//clear
}
#define SECOND_LINE		0x040
//write to line [0..1], c[0..16]
void dprint_move_to(unsigned char line,unsigned char c){
	if(line==1) dprint_write_byte(0x80 | SECOND_LINE | c,1);
	else dprint_write_byte(0x80 | c,1);
}
//write one char
void dprint_char(char d){
	dprint_write_byte(d,0);
}
//write a string to the display
void dprint(char* str){
	int i;
	int max;
	if(strlen(str)>16) max = 16;
	else max = strlen(str);
	for (i=0;i<max;i++) dprint_char(str[i]);
	for (i=max;i<16;i++) dprint_char(' ');

}

/*******************************************************************
   Time Functions
 ******************************************************************/
//print the time to a string
void sprint_time(char *buffer){
	unsigned long thetime;
	struct tm	thetm;
	thetime = read_rtc();
	mktm(&thetm, thetime);
	//sprintf(buffer,"@ %04d.%02d.%02d - %02d:%02d:%02d",
	//		1900+thetm.tm_year, thetm.tm_mon, thetm.tm_mday,
	//		thetm.tm_hour, thetm.tm_min, thetm.tm_sec);
	sprintf(buffer,"%02d:%02d",thetm.tm_hour, thetm.tm_min);
}

//print the time to standard out
void print_time(unsigned long thetime){
	struct tm	thetm;
	mktm(&thetm, thetime);
	printf("%02d/%02d/%04d %02d:%02d:%02d\n",
			thetm.tm_mon, thetm.tm_mday, 1900+thetm.tm_year,
			thetm.tm_hour, thetm.tm_min, thetm.tm_sec);
}
//set the time from an internet NIST server
#define NIST_SERVER		"128.138.140.44"
#define NIST_PORT		13
#define NIST_TIMEZONE		-5
void set_nist_time(){
	int dst, health;
	struct tm	t;
	unsigned long	longsec;
	char response[100];
	int bytes_read,total_bytes;
	tcp_Socket sock;
	longword nist_server;
	printf("Trying to set time from NIST server...\n");
	//get the time
	if (!(nist_server = resolve(NIST_SERVER))) {
        printf(" ! Could not resolve time host\n");
        exit( 3 );
    }
	if( !tcp_open(&sock,0,nist_server,NIST_PORT,NULL)){
		printf(" ! Unable to connect to time server\n");
		exit( 3 );
	}
	while (!sock_established(&sock) && sock_bytesready(&sock)==-1){
       tcp_tick(NULL);
    }
	sock_mode(&sock, TCP_MODE_ASCII);
	total_bytes=0;
	do{
		bytes_read=sock_fastread(&sock,response+total_bytes,sizeof(response)-1-total_bytes);
		total_bytes+=bytes_read;
	} while(tcp_tick(&sock) && (total_bytes < sizeof(response)-2));
	//parse it
	t.tm_year = 100 + 10*(response[7]-'0') + (response[8]-'0');	
	t.tm_mon  = 10*(response[10]-'0') + (response[11]-'0');
	t.tm_mday = 10*(response[13]-'0') + (response[14]-'0');
	t.tm_hour = 10*(response[16]-'0') + (response[17]-'0');
	t.tm_min  = 10*(response[19]-'0') + (response[20]-'0');
	t.tm_sec  = 10*(response[22]-'0') + (response[23]-'0');
	dst       = 10*(response[25]-'0') + (response[26]-'0');
	health    = response[28]-'0';
	longsec = mktime(&t);
	longsec += 3600ul * NIST_TIMEZONE;		// adjust for timezone
	if (dst != 0) longsec += 3600ul;	// DST is in effect
	if (health < 2) write_rtc(longsec);
	printf("  Time set to : ");
	print_time(read_rtc());
}

/*******************************************************************
   Network Utility Functions
 ******************************************************************/
//print out useful network information
static void print_results(){
	auto long tz;
	char ipstr[20];		//network variables
	inet_ntoa((char *)&ipstr,my_ip_addr);
	printf("  Network Parameters:\n");
	printf("    My IP Address = %s (%08lX)\n", ipstr, my_ip_addr);
	printf("    Netmask = %08lX\n", sin_mask);
	if (_dhcphost != ~0UL) {
		if (_dhcpstate == DHCP_ST_PERMANENT) {
			printf("    Permanent lease\n");
		} else {
			printf("    Remaining lease = %ld (sec)\n", _dhcplife - SEC_TIMER);
			printf("    Renew lease in %ld (sec)\n", _dhcpt1 - SEC_TIMER);
		}
		printf("    DHCP server = %08lX\n", _dhcphost);
		printf("    Boot server = %08lX\n", _bootphost);
	}
	if (gethostname(NULL,0))
		printf("    Host name = %s\n", gethostname(NULL,0));
	if (getdomainname(NULL,0))
		printf("    Domain name = %s\n", getdomainname(NULL,0));
	if (dhcp_get_timezone(&tz))
		printf("    Timezone (fallback only) = %ldh\n", tz / 3600);
	else
		printf("    Timezone (DHCP server) = %ldh\n", tz / 3600);
	if (_last_nameserver)
		printf("    First DNS = %08lX\n", def_nameservers[0]);
	if (_smtpsrv)
		printf("    SMTP server = %08lX\n", _smtpsrv);
}
//grab a DCHP address
static int init_DHCP(){
	int status;
	_survivebootp = 1;// Set to 1 (default) or 0 to never use fallbacks
	_bootptimeout = 6;// Short timeout for testing
	printf("Starting network (max wait %d seconds)...\n", _bootptimeout);
	status = sock_init();
	switch (status) {
		case 1:
			printf("! Could not initialize packet driver.\n");
			return 0;
		case 2:
			printf("! Could not configure using DHCP.\n");
			break;	// continue with fallbacks
		case 3:
			printf("! Could not configure using DHCP; fallbacks disallowed.\n");
			return 0;
		default:
			break;
	}
	if (_dhcphost != ~0UL)
		printf("  Lease obtained\n");
	else {
		printf("! Lease not obtained.  DHCP server may be down.\n");
		printf("! Using fallback parameters...\n");
		return 0;
	}
	print_results();
	set_nist_time();	//and now set the time...
	return 1;
}

/*******************************************************************
   Misc Utility Functions
 ******************************************************************/
static void substr(char* dest,char* str,int begin, int end){
	int i;
	for (i=0;i<(end-begin);i++){
		dest[i] = str[i+begin];
	}
	dest[i] = 0;
}


/*******************************************************************
   Program Global Variables
 ******************************************************************/
char headlines[1000];

/*******************************************************************
   RSS Fetching...
 ******************************************************************/
//http://rss.com.com/2547-12-0-5.xml - cnet
#define RSS_REFRESH_DELAY	300		//5 mins, in seconds
#define RSS_SLASHDOT_SERVER		"slashdot.org"
#define RSS_SLASHDOT_PAGE		"/slashdot.rdf"
#define RSS_CNN_SERVER		"csociety.purdue.org"
#define RSS_CNN_PAGE		"/~jacoby/XML/CNN_TOP_STORIES.xml"
#define RSS_BBC_SERVER		"www.bbc.co.uk"
#define RSS_BBC_PAGE		"/syndication/feeds/news/ukfs_news/world/rss091.xml"

static int get_headlines(char *rss_site,char *rss_page){
	longword rss_server;	//holds resolve(RSS_SERVER)
	int bytes_read,total_bytes;		//for sock_fastread
	int count;
	tcp_Socket socket;		//for comm
	char rssCode[6500];		//7kb max downloaded file limit
	unsigned int pos;
	char temp[500];
	char* newStr;
	char otherStr[500];
	printf("Fetching headlines...\n");
	if (!tcp_tick(NULL)) {
        printf(" ! No network");
        return 0;
	}
	//connect to the server
	if (!(rss_server = resolve(rss_site))) {
        printf(" ! Could not resolve host");
        return 0;
    }
	if( !tcp_open(&socket,0,rss_server,HTTP_PORT,NULL)){
		printf(" ! Unable to connect to remote server");
		return 0;
	}
	//wait for the connection to be established
	while (!sock_established(&socket) && sock_bytesready(&socket)==-1){
       tcp_tick(NULL);
    }
	printf("  connected! sending request\n");
	//get the RSS code
	memset(rssCode,0,sizeof(rssCode));	//empty it first!
	sock_puts(&socket,"GET ");
	sock_puts(&socket,rss_page);
	sock_puts(&socket," HTTP/1.0\n");
	sock_puts(&socket,"Accept: text/xml\nAccept: text/plain\nAccept: application/rss+xml\n\n");
	sock_puts(&socket,"User-Agent: Mozilla/5.0 (Windows; U; Win98; en-US; m18) Gecko/20001108 Netscape6/6.0 \r\n\r\n");
	total_bytes=0;
	do{
		//this should be changed to parse as we receive each set of
		//bytes, so we don't need to waste 10k of memory with the big
		//rssCode array...
		bytes_read=sock_fastread(&socket,rssCode+total_bytes,sizeof(rssCode)-1-total_bytes);
		total_bytes+=bytes_read;
	} while(tcp_tick(&socket) && (total_bytes < sizeof(rssCode)-2));
	printf("  Connection closed (received %d bytes)...\n",total_bytes);
	if(total_bytes<600) printf(rssCode);
	//parse out the headlines
	memset(headlines,0,sizeof(headlines));
	strcat(headlines,"** ");
	newStr = strstr(rssCode,"<title>");
	newStr = strstr(newStr,">");
	count=0;
	while((newStr!= NULL)){
		pos = strcspn(newStr,"<");
		temp[0] = 0;
		strncat(temp,newStr,pos);
		substr(otherStr,temp,1,strlen(temp));
		printf("  - \"%s\"\n",otherStr);	//debug - print out the headline
		strcat(headlines,otherStr);
		strcat(headlines,"...  ");
		newStr = strstr(newStr,"</title>");
		newStr = strstr(newStr,"<title>");
		newStr = strstr(newStr,">");
		count++;
	}
	printf("Parsed out %d headlines.\n\n",count);
	return 1;
}

/*******************************************************************
   iDisplay functions
 ******************************************************************/
static void dprint_info(){
	char line[17];
	dprint_clear();
	dprint_move_to(0,0);
	dprint("iDisplay");
	dprint_move_to(0,12);
	dprint("v0.1");
	inet_ntoa((char *)&line,my_ip_addr);
	dprint_move_to(1,1);
	dprint(line);
}

/*******************************************************************
   Program Main
 ******************************************************************/
main() {
	int i;	// define an integer j to serve as a loop counter
	//display variables
	char *strPtr, *scrollStrPtr;
	int dpos;
	short scroll;
	//program variables
	int lastSwitchVal;
	long timeStamp,st;
	char line1[17];
	char *rssServer,*rssPage;

	//display init
	scroll=1;
	dprint_init_ports();
	dprint_init();
	printf("LCD display initialized.\n");
	//print a welcome message
	dprint_clear();
	dprint_move_to(0,0);
	dprint("iDisplay");
	dprint_move_to(0,12);
	dprint("v0.1");
	dprint_move_to(1,0);
	dprint("initializing...");

	//network init
	strPtr="";
	if(init_DHCP()) {
		dprint_info();
		strPtr = "success!";
	} else {
		strPtr = "** nework initialization error!";
	}

	//program init
	timeStamp = SEC_TIMER;	//set up the timing for refreshing the data...
	lastSwitchVal = 1234;	//random, so first time it grabs RSS
	memset(headlines,0,sizeof(headlines));	//just to be safe...

	//example message
	scrollStrPtr = strPtr;
	dpos = 15;

	while(1){

		costate{
			//grab the headlines after 5 mins, or on a switch change
			if((lastSwitchVal != BitRdPortI(PBDR,2)) ||
				(SEC_TIMER > timeStamp+RSS_REFRESH_DELAY) ){
				scroll=0;
				dprint_clear();
				dprint_move_to(1,0);
				dprint("Loading data...");
				if (lastSwitchVal){
					rssServer=RSS_BBC_SERVER;
					rssPage=RSS_BBC_PAGE;
					strcpy(line1,"BBC News");
				} else {
					rssServer=RSS_CNN_SERVER;
					rssPage=RSS_CNN_PAGE;
					strcpy(line1,"CNN");
				}
				if(get_headlines(rssServer,rssPage)) strPtr = headlines;
				else {
					strPtr = "** unable to connect to Internet!";
					strcpy(line1,"Error");
				}
				scrollStrPtr = strPtr;	//be friendly and leave strPtr untouched
				lastSwitchVal = BitRdPortI(PBDR,2);
				timeStamp = SEC_TIMER;
				dprint_move_to(0,0); //print the data source
				dprint(line1);
				sprint_time(line1);	//now print the time
				dprint_move_to(0,11);
				dprint(line1);
				scroll=1;
			}
		}

		costate{
			//scroll a message across the screen
			if(scroll==1){
				dprint_move_to(1,0);
				for(i=0;i<dpos;i++) dprint_char(' ');
				if(dpos > 0) dpos--;
				if(dpos == 0) scrollStrPtr++;
				dprint(scrollStrPtr);
				if(*scrollStrPtr==0) {	//end of str
					scrollStrPtr = strPtr;
					dpos = 15;
				}
				waitfor(DelayMs(150));
			}
		}

		costate{
			//debugging
			if (BitRdPortI(PBDR, 3)==0){
				while(BitRdPortI(PBDR, 3)==0);	//debounce
				scroll=0;
				dprint_info();
			}
		}

	}	//while true

} // end program
