/*  DISCOGS.C  - process the discogs.com release database (xml) file and extract
    entries for a specific artist <id>.


compile with visual studio 2015 community at the command line, and run on Win7.
cl discogs.c




Usage
 discogs infile outfile csvfile


Precautions and limitations
- does not check outfile or csvfile, just overwrites.  don't mess up.

Revision history

0.1  09/09/18 blk.  Added parsing of XML for the creation of CSV file
	passed on command line as 3rd parameter.
	Previous version only removed the specified XML.

0.0  06/28/18 blk.  Creation from peco.c 0.2. (saved around 03/05/12).
	Initial version.  Processes inputfile, searches both single line
	and multiline entries of releases (looking for SEARCH_START and
	SEARCH_END) and if the text between those delimiters contains
	SEARCH_STRING, it will write that into the outfile.
	This cuts the file size down from 36+ GB to something manageable
	with a regular editor.
	This is roughly equivalent to searching the file with a regex,
	and was necessary to code it because in the only functional program
	that supports regular expressions I found that can open such huge files,
	glogg, doesn't support multiline regular expressions and therefore
	misses many entries from the releases database.

	This is a functional regex that would do what this program does, if
	there were a tool to support running it on a 36GB file:

	<release id=.*?16655</id>[ \s\S\r\n]+</release>
	(might need it to be <id?16655</id>)
	A test file: http://debbieharry.net/transfer/regex-test-excerpt2
	Note: mode is global, multiline, ungreedy.


Operation:
File is opened and read in blocks of BLOCKSIZE (1024KB) into a static buffer.
Less data could be read if EOF is encountered.
(An assumption is made that the xml of all releases is less than that size).
Search for the SEARCH_START string in the buffer, then from that point, search
for the SEARCH_END string.
If the SEARCH_STRING is contained between the string between those points,
write it to the outfile.
If it is not contained, it's someone else's release and is not written to outfile.

Proceed to search again (in the buffer) starting at the previous SEARCH_END point.
If a matching SEARCH_END is not found in the buffer, the data is discarded and
and a new block is read from the infile beginning at the SEARCH_START point.
A new block is also read if the SEARCH_START is not found in the remaining buffer.

Continue until EOF.

Beware: buffer block length is hardcoded.  It must be at least the length of
the longest release data.  Does not error check for missing/mismatched start/end.
50000 is not enough - release 13117 (2018-Jun-01) is too large.
1310172 is also too small - release 43501 is too large.
Try 2^18 (262144).  Found 21 releases among 123692 OK.//(not large enough -- see release id="2626057" (2^18)
//#define BLOCKSIZE 524288 //(not large enough -- see release id="7910952"  (2^19)
Ended up with this:
#define BLOCKSIZE 1048576  // (2^20) is enough.

TTD: pass search string on command line

Tested working with Discogs release database xml file (about 35GB) from June 2018.
Output file is about 55MB, 5810 releases found out of 9906032 releases.

Optionally writes a debug.txt file that contains a list of the releases saved.



*/

#define _CRT_SECURE_NO_WARNINGS 1

#include<dos.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<fcntl.h>
#include<time.h>



#define VERSION "DISCOGS Release database XML search processor, version 0.1"


#define SEPARATOR "	"
#define HEADER_LINE "\"release_id\"	\"title\"	\"released\"	\"country\"	\"notes\"	\"data_quality\"\
	\"first_label&catno\"	\"first_label\"	\"first_catno\"	\"all_label&catno\"\
	\"format_name\"	\"format_qty\"	\"format_text\"	\"description\"	\"combined_description\"\
\n"
#define TITLE_START "<title>"
#define TITLE_END "</title>"
#define RELEASED_START "<released>"
#define RELEASED_END "</released>"
#define COUNTRY_START "<country>"
#define COUNTRY_END "</country>"
#define NOTES_START "<notes>"
#define NOTES_END "</notes>"
#define DATA_QUALITY_START "<data_quality>"
#define DATA_QUALITY_END "</data_quality>"

// for nested xml items
#define LABELS_START "<labels>"
#define LABELS_END "</labels>"

#define EMPTY_FIELD "\" \""
#define LABEL_CATNO_SEPARATOR "--"

#define FORMAT_NAME_START "<format name=\""
#define FORMATS_END "</formats>"




//display progress of found search strings
#define DEBUG_SEARCH_RESULTS 0
//quit early
#define TEST_MODE 0
//show memmem() call parameters
#define DEBUG_MEMMEM 0
//show file progress (x100 releases, x10 found)
#define DEBUG_PROGRESS 1
//abort if buffer is too small (causes infinite fseek/fread loop)
#define DEBUG_INFINITELOOP 1
//abort if remaining buffer size>blocksize (problem with size of variables?)

#define DEBUG_BUFFER_OVERRUN 1

#define DEBUG_FINDS 1
//print out info if something is found

// write file with found release IDs and error messages
#define WRITE_DEBUG_FILE 1


#define SEARCH_START "<release id="
#define SEARCH_END "</release>"
//#define SEARCH_STRING "16655</id>"
#define SEARCH_STRING "<artist><id>16655</id>"


#define STOP_SEARCHING_THRESHOLD 10
// how close to end of buffer when a read next block is triggered

//#define BLOCKSIZE 131072
//#define BLOCKSIZE 50000
//#define BLOCKSIZE 262144 //(not large enough -- see release id="2626057" (2^18)
//#define BLOCKSIZE 524288 //(not large enough -- see release id="7910952"  (2^19)
#define BLOCKSIZE 1048576  // (2^20) is enough.


/*--- proto --------------------------------------------------*/

void closefiles(void);
void syntax(void);
void initialize(void);
void execute(void);
void terminate(void);

void process_input_file();
void process_xml(unsigned char *foundstartptr,size_t searchresultlen);

void *memmem(unsigned char *haystack, size_t hlen, unsigned char *needle, size_t nlen);

/*------------------------------------------------------------*/



/*-- variables -----------------------------------------------*/


const char *newline="\n";

unsigned int err;

unsigned int errorcode=0;

int writing_file;
int done;
int eof_encountered;
unsigned int i,j,k;
int result;
unsigned char ch;

unsigned char infilename[100];
unsigned char outfilename[100];
unsigned char csvfilename[100];
unsigned char debugfilename[100];
#define DEBUGFILENAME "debug.txt"

unsigned char *strbuf;
unsigned char *nextpart;

unsigned long readblockcount;
unsigned long releasecount;
unsigned long foundcount;
unsigned long errorcount;
unsigned int found;
int searchstringlen;
int startstringlen;
int endstringlen;

int xmlstartstringlen;
int xmlendstringlen;

unsigned long long fileposition;
long int fileoffset;
size_t readresult;

unsigned long inputbufferposition; // location to begin or continue searching
unsigned longstartsearchposition;  // location of startsearchposition within inputbuffer
unsigned longstartsearchposition;  // location of endsearchposition within inputbuffer
//unsigned int foundstartsearchposition, foundendsearchposition; // flags for found or not
unsigned long lastendsearchposition;

clock_t begin_time,end_time,execution_time;

unsigned char inputbuffer[BLOCKSIZE+2];
unsigned char* beginbuffersearchat;
unsigned char searchbuffer[1000];
unsigned char startsearchbuffer[1000];
unsigned char endsearchbuffer[1000];

unsigned char xmlfindstartbuffer[1000];
unsigned char xmlfindendbuffer[1000];

unsigned char tempbuffer[BLOCKSIZE+2];

unsigned char *foundstartptr;
unsigned int foundstartptrvalid;
size_t remainingbufferlen;
unsigned char *foundendptr;
unsigned char *lastfoundendptr;
size_t searchresultlen;
unsigned char *foundendptr;
unsigned int foundendptrvalid;
unsigned int foundendptrvalid;
unsigned char *foundsearchstringptr;
unsigned char *foundxmlstringptr;
unsigned char *foundxml2stringptr;

unsigned long release_id;
unsigned long looking_for_releaseid;
unsigned long rel_id;

size_t writesuccess;

long currentfileposition;

FILE *infile;
FILE *outfile;
FILE *csvfile;
FILE *debugfile;

// mostly temporary variables

#define MAX_CATNO_COUNT	3
#define MAX_CATNO_LEN	40
#define MAX_LABELNAME_LEN	80

#define MAX_FORMAT_COUNT	20
#define MAX_FORMAT_LEN	40

unsigned char catno[MAX_CATNO_COUNT][MAX_CATNO_LEN];
unsigned char labelname[MAX_CATNO_COUNT][MAX_LABELNAME_LEN];
unsigned char format[MAX_FORMAT_COUNT][MAX_FORMAT_LEN];
unsigned int catno_count;
unsigned int format_count;
unsigned int nx;
unsigned char *tptr;
unsigned char *currentptr;
unsigned char *labelnameptr;
unsigned char *xptr;

unsigned char format_name[100];
unsigned char format_qty[100];
unsigned char format_text[1000]; //what is this?
unsigned int format_desc_count;

unsigned char *qtyptr;  // formats qty=

#define MAX_DESCRIPTION_COUNT 50
#define MAX_DESCRIPTION_LEN 50
unsigned char description[MAX_DESCRIPTION_COUNT][MAX_DESCRIPTION_LEN];
#define FORMAT_DESCRIPTION_SEPARATOR ", "

/*------------------------------------------------------------*/


main(argc,argv)
int argc;
char *argv[];
{

	initialize();


	switch (argc)
		{
		case 4:
			{
			strcpy(infilename,argv[1]);
			strcpy(outfilename,argv[2]);
			strcpy(csvfilename,argv[3]);
			strcpy(debugfilename,DEBUGFILENAME);

//			printf("infilename=%s\n",infilename);
			infile=fopen(infilename,"rb");
			if (infile==NULL)
				{
				printf("Error: input file %s not found.\n",infilename);
				errorcode=1;
				break;
				}
#if WRITE_DEBUG_FILE
			printf("debug output directed to %s\n",debugfilename);
#endif
			printf("XML output directed to %s\n",outfilename);
			printf("CSV output directed to %s\n",csvfilename);
			outfile = fopen(outfilename, "wb");
			csvfile = fopen(csvfilename, "wb");
			writing_file=1;
			if (outfile==NULL)
				{
				writing_file=0;
				errorcode=2;
				printf("outfile failed!\n");
				fclose(infile);
				break;
				}
			if (csvfile==NULL)
				{
				writing_file=0;
				errorcode=3;
				printf("csvfile failed!\n");
				fclose(infile);
				break;
				}
			fprintf(outfile,"\n");
			fprintf(outfile,"%s\n", VERSION);
			fprintf(csvfile,"\n");
			fprintf(csvfile,"%s\n", VERSION);
			printf("Using input file %s\n", infilename);
			printf("Search string: \"%s\"\n", SEARCH_STRING);
			printf("Searching between \"%s\" and \"%s\"\n", SEARCH_START, SEARCH_END);
			fprintf(outfile,"Using input file %s\n", infilename);
			fprintf(outfile,"Search string: \"%s\"\n", SEARCH_STRING);
			fprintf(outfile,"Searching between \"%s\" and \"%s\"\n", SEARCH_START, SEARCH_END);
			fprintf(outfile,"\n\n");

			fprintf(csvfile,"Using input file %s\n", infilename);
			fprintf(csvfile,"Search string: \"%s\"\n", SEARCH_STRING);
			fprintf(csvfile,"Searching between \"%s\" and \"%s\"\n", SEARCH_START, SEARCH_END);
			fprintf(csvfile,"\n\n");
			fprintf(csvfile,HEADER_LINE);

#if WRITE_DEBUG_FILE
			debugfile = fopen(debugfilename, "wb");
			if (debugfile==NULL)
				{
				printf("debugfile failed!\n");
				errorcode=4;
				fclose(infile);
				fclose(outfile);
				fclose(csvfile);
				break;
				}
			fprintf(debugfile,"\n");
			fprintf(debugfile,"%s\n\n", VERSION);
			fprintf(debugfile,"Using input file %s\n", infilename);
			fprintf(debugfile,"Debug output of found releases and errors:\n\n");
#endif

			execute();
			break;

			}
		default:
		case 1:
		case 2: {syntax(); break;}
		 } //end switch argc


	exit(errorcode);

} //end main()


void syntax(void)
{
	printf("%s\n",VERSION);
	printf("syntax:  DISCOGS infile outfile csvfile\n\n");
	printf("   infile  = XML dump of discogs.com release database to be searched for artist\n");
	printf("   outfile = output file for storing xml search results.\n");
	printf("   csvfile = output file for storing csv data.\n");
	printf("\n");
	printf("Compiled to search for:\n");
	printf("   \"%s\"\n", SEARCH_STRING);
	printf("   between: \"%s\"\n", SEARCH_START);
	printf("   and: \"%s\"\n", SEARCH_END);
	printf("\n");

	printf("\nBLOCKSIZE is %lu\n\n",BLOCKSIZE);
	printf("For more information, READ THE SOURCE CODE.\n");
	exit(3);
}


void execute(void)
{
	process_input_file();
	closefiles();
	terminate();
}
void process_input_file()
{
// assumes open input file "infile".
	printf("Searching input file	%s: \n",infilename);
	fprintf(outfile,"Searching input file	%s: \n\n",infilename);

	begin_time=clock();
	readblockcount=1;
	fileposition=0; // this is how far into the input file that data has been searched xyzzy not needed???
// is a previous copy of it needed??

	currentfileposition=ftell(infile);
#if DEBUG_SEARCH_RESULTS
	printf("Prior to read file loop, file position=%li\n",currentfileposition);
#endif

	do
		{
		memset(inputbuffer, 0x00, BLOCKSIZE*sizeof(char));
		done=0; // flag for done searching input buffer
		lastendsearchposition=0;
		beginbuffersearchat=inputbuffer;
		remainingbufferlen=BLOCKSIZE;

		readresult=fread(&inputbuffer, BLOCKSIZE, 1, infile);
#if DEBUG_SEARCH_RESULTS
		printf("fread returned %zu blocks read from fileposition=%llu\n",readresult,fileposition);
#endif
		if (!readresult)
			{
			eof_encountered=1;
			printf("fread returned error, last search pass starting\n");
			}
		inputbufferposition=0; // searches of new read blocks always start at beginning of buffer
#if DEBUG_SEARCH_RESULTS
		printf("read file to inputbuffer, block %u\n", readblockcount);
#endif
		readblockcount++;
		fileposition+=BLOCKSIZE; //
		currentfileposition=ftell(infile);
#if DEBUG_SEARCH_RESULTS
		printf("Inside read file loop, file position=%li\n",currentfileposition);

printf("Last read returned inputbuffer:\n");	
for (i=0;i<120;i++) printf ("%c",inputbuffer[i]);
printf("\n");
#endif

while (!done)
{
//search inputbuffer for match to startssearchbuffer
//when found, search from there for match to endsearchbuffer.
//When found, search between those locations for searchbuffer.
// If found in there, write to outfile
// don't run past end of the buffer, figure out how to trigger next file read:
// - if current search pointer exceeds end of inputbuffer before finding endsearchbuffer:
//   compute position in file where the start of the last found startsearchbuffer is and
//   fseek the file to that position and re-fread infile.
//
//
//

//	foundstartsearchposition=lastendsearchposition; // pick up where the search left off
//	foundendsearchposition=0;
	foundstartptrvalid=0;
	foundendptrvalid=0;
	foundstartptr=memmem(beginbuffersearchat, remainingbufferlen, startsearchbuffer, startstringlen);

	if (foundstartptr!=NULL)
		{
		foundstartptrvalid=1;

#if DEBUG_SEARCH_RESULTS
		release_id=strtoul(foundstartptr+13,NULL,10);
		if (release_id==looking_for_releaseid)
			{
			printf("Found release id %u\n",looking_for_releaseid);
			}
		printf("S Found start string in releaseid %u at buffer position %lu ; remainingbuflen=%u\n",release_id,(foundstartptr-inputbuffer),remainingbufferlen);
//		ch=getchar();
#endif
		// now search for endstring
		foundendptr=memmem(foundstartptr+startstringlen, remainingbufferlen, endsearchbuffer, endstringlen);
		if (foundendptr!=NULL)
			{
			foundendptrvalid=1;
			lastfoundendptr=foundendptr+endstringlen;  //xyzzy
			lastendsearchposition=foundendptr-foundstartptr+endstringlen; //<--- error
//			beginbuffersearchat+=(lastendsearchposition);
			beginbuffersearchat=(foundendptr+endstringlen);  // used for next iteration of buffer search

			remainingbufferlen=(inputbuffer+BLOCKSIZE-foundendptr);
#if DEBUG_SEARCH_RESULTS
	printf("Remaining buffer length to search is %lu  ;  done=%u\n",remainingbufferlen, done);
#endif
			if (remainingbufferlen<STOP_SEARCHING_THRESHOLD)
				{
				done=1;
#if DEBUG_SEARCH_RESULTS
	printf("Remaining buffer length %lu is too small to continue searching;  done=%u\n",remainingbufferlen, done);
#endif
				}

#if DEBUG_BUFFER_OVERRUN
if (remainingbufferlen>BLOCKSIZE)
{
	printf("ERROR!  remainingbufferlen too large, =%lu\n",remainingbufferlen);
	printf("SIZE_MAX=%lu\n",SIZE_MAX);
	printf("foundstartptr=%lu\n",(unsigned long)(foundstartptr-inputbuffer));
	printf("data at foundstartptr:\n");
	for (i=0;i<120;i++) printf ("%c",*foundstartptr++);
	printf("\n");
	printf("beginbuffersearchat=%lu\n",(unsigned long)(beginbuffersearchat-inputbuffer));
	printf("Last read returned inputbuffer:\n");	
	for (i=0;i<120;i++) printf ("%c",inputbuffer[i]);
	printf("\n");
	printf("closing files.  processed %lu releases, matched %lu releases\n",	releasecount,foundcount);
	fclose(infile);
	fclose(outfile);
	fclose(csvfile);
#if WRITE_DEBUG_FILE
	fclose(debugfile);
#endif

	exit(1);
}
#endif


#if DEBUG_SEARCH_RESULTS
			printf("E LESP=%lu, remainingbuflen=%u  , set bbs-at to %lu",lastendsearchposition,remainingbufferlen,(unsigned long)beginbuffersearchat);
#endif
			releasecount++;
#if DEBUG_PROGRESS
			if (0==releasecount%100)
				{
				printf(" r%lu ",releasecount);
				}
#endif
			if (remainingbufferlen<STOP_SEARCHING_THRESHOLD)
				{
				done=1;
#if DEBUG_SEARCH_RESULTS
				printf("- set done flag - ");
#endif
				}
#if DEBUG_SEARCH_RESULTS
			printf("\nFound end of endstring at %u\n",lastendsearchposition);
#endif
			searchresultlen=foundendptr-foundstartptr+endstringlen;
#if DEBUG_SEARCH_RESULTS
			printf("Search result string is	%u characters long.\n",searchresultlen);
#endif
			foundsearchstringptr=memmem(foundstartptr, searchresultlen , searchbuffer, searchstringlen);
			if (foundsearchstringptr==NULL)
				{
#if DEBUG_SEARCH_RESULTS
				printf("Search string returned NULL\n");
#endif
				}
			else
				{
#if DEBUG_SEARCH_RESULTS
				printf("Found search string within bounds at position %u\n",(foundsearchstringptr-inputbuffer));
#endif
				foundcount++;
#if DEBUG_FINDS
		  		release_id=strtoul(foundstartptr+13,NULL,10);
				printf("Found search string within bounds at position %u\n",(foundsearchstringptr-inputbuffer));
				printf("Foundcount: %lu   Releasecount: %lu\n",foundcount,releasecount);
				printf("S Found start string in releaseid %u at buffer position %lu ; remainingbuflen=%u\n",release_id,(foundstartptr-inputbuffer),remainingbufferlen);
				printf("E LESP=%lu, remainingbuflen=%u  , set bbs-at to %lu\n",lastendsearchposition,remainingbufferlen,(unsigned long)beginbuffersearchat);
#endif
#if WRITE_DEBUG_FILE
				fprintf(debugfile,"<release id=\"%u\"\n",release_id);
#endif



#if DEBUG_PROGRESS
				if (0==foundcount%10)
					{
					printf("\nf%lu ",foundcount);
					}
#endif

				// process XML into CSV data
				process_xml(foundstartptr,searchresultlen);

				// write the data to output file
				writesuccess=fwrite(foundstartptr,searchresultlen,1,outfile);
				fwrite(newline,1,1,outfile);
				if (writesuccess==1)
					{
#if DEBUG_SEARCH_RESULTS
					printf("successfully wrote found record %u to outfile ******************\n",foundcount);
//					exit(0);
#endif
					}
				else
					{
					errorcount++;
					printf("Error %u: failed to write found [r%u], record %u to outfile\n",errorcount,release_id,foundcount);
#if WRITE_DEBUG_FILE
					fprintf(debugfile,"Error %u: failed to write found [r%u], record %u to outfile\n",errorcount,release_id,foundcount);
#endif
					}
#if TEST_MODE
if (foundcount>9)
	{
	closefiles();
	end_time=clock();
	printf("\n");
	execution_time=end_time-begin_time;
	printf("Execution time: %u\n",execution_time);
	exit(0);
	}
#endif
				} //foundsearchstringptr true
// Here, need to reinitialize valid flags and continue through the search buffer.
// Need to invalidate pointers and flags; and ideally, continue searching through the inputbuffer from where it left off,
//    before loading a new file block.  However, as an unoptimized solution, could calculate an offset and do an fseek back to the last foundendptr.
// The search left off at foundendptr.  Must look for new foundstartptr at that point.
// Also: save the last foundendptr.  Continue from that point forward. XXXXXXXXXXXX check this (seems no need to have valid flags)
			} //foundendptr true
		else
			{
			//Here, startstring was found, but endstring was not.
			//Offset the file back to the location of startstring, and read it into the beginning of the buffer so it all fits.
			//On the next loop, startstring should always be found at position 0 in inputbuffer.
			if (fileposition>0)
				{
				fileoffset=-(BLOCKSIZE-(foundstartptr-inputbuffer));
				fileposition+=fileoffset;
			if (eof_encountered)
				{
				printf("Note: EOF was encountered, but a realignment was required to process the remaining buffer.  Clearing EOF\n");
				eof_encountered=0;
				}

#if DEBUG_INFINITELOOP
#if DEBUG_SEARCH_RESULTS
				printf("endstring not found, setting file offset to %li (startstring should be found at pos 0 on next loop)\n",fileoffset);
#endif
				// quit if buffer is too small to read the release, as this causes an infinite file seek/fread loop:
				if (0==(foundstartptr-inputbuffer))
					{
					printf("\nError: infinite loop.  BLOCKSIZE too small to accommodate release data.  Aborting.\n");
					exit(5);
					}
#endif
				result=fseek(infile,fileoffset,SEEK_CUR);
#if DEBUG_SEARCH_RESULTS
				if (result==0) printf(" - succeeded \n"); else printf(" - failed\n");
#endif

				currentfileposition=ftell(infile);
#if DEBUG_SEARCH_RESULTS
				printf("After fseek1, file position=%li\n",currentfileposition);
#endif

				foundstartptr=NULL;  // clear for next loop
				foundstartptrvalid=0;  // clear validity

				done=1;
//				continue;
				}
			}//foundendptr false
		} //foundstartptr true
	else
		{
		// didn't find the startstring.  offset file position pointer by -startstringlen (in case the start string spans the two blocks)
		// If there is no startstring, one possibility is EOF is encountered and there is no more data
#if DEBUG_SEARCH_RESULTS
			printf("startstring not found\n");
#endif
		if (eof_encountered)
			{
			printf("EOF encountered and start string not found, exiting\n");
			done=1;
			}
		else if (fileposition>0) // other possibility is the endstring did not fit in the buffer, so reread it to beginning of buffer
			{
			fileoffset=-startstringlen;
			fileposition+=fileoffset;
#if DEBUG_SEARCH_RESULTS
			printf("startstring not found, setting file offset to %li\n",fileoffset);
#endif
			result=fseek(infile,fileoffset,SEEK_CUR);

			currentfileposition=ftell(infile);
#if DEBUG_SEARCH_RESULTS
			printf("After fseek2, file position=%li\n",currentfileposition);

			if (result==0) printf(" - succeeded \n"); else printf(" - failed\n");
#endif
			done=1;
//			continue;
			}
		} //foundstartptr false


} // while !done

	} while (readresult);


//xyzzy
	end_time=clock();
	printf("End of file encountered at readblockcount %u\n",readblockcount);
#if DEBUG_SEARCH_RESULTS
	printf("Last read returned inputbuffer:\n");	
	for (i=0;i<120;i++) printf ("%c",inputbuffer[i]);
	printf("\n");
#endif
	execution_time=end_time-begin_time;
	printf("Execution time: %u\n",execution_time);
	printf("Saved %lu releases containing searchstring among %lu total releases.\n",foundcount,releasecount);
	printf("Press Enter to continue\n");
	ch=getchar();

// finish searching the buffer

	


} // end execute


void terminate(void)
{
	exit(0);
}


void initialize(void)
{
	printf("Initializing.\n");
#if DEBUG_SEARCH_RESULTS
	looking_for_releaseid=999;
#endif
	errorcode=0;
	readblockcount=0;
	releasecount=0;
	foundcount=0;
	writing_file=0;
	eof_encountered=0;
	done=0;
	foundstartptrvalid=0;
	foundendptrvalid=0;
	errorcount=0;

	fileposition=0;

	searchstringlen=strlen(SEARCH_STRING);
	printf("searchstringlen=%u\n",searchstringlen);
	strcpy(searchbuffer,SEARCH_STRING);

	startstringlen=strlen(SEARCH_START);
	printf("startringlen=%u\n",startstringlen);
	strcpy(startsearchbuffer,SEARCH_START);

	endstringlen=strlen(SEARCH_END);
	printf("endstringlen=%u\n",endstringlen);
	strcpy(endsearchbuffer,SEARCH_END);

}

void closefiles(void)
{
	printf("close files.  processed %lu releases, matched %lu releases\n",	releasecount,foundcount);
	fprintf(outfile,"\n\nclose files.  processed %lu releases, matched %lu releases\n",	releasecount,foundcount);
	fprintf(outfile,"%u errors.\n",errorcount);
	fclose(infile);
	fclose(outfile);
#if WRITE_DEBUG_FILE
	fprintf(debugfile,"\n\nclose files.  processed %lu releases, matched %lu releases\n",	releasecount,foundcount);
	fprintf(debugfile,"%u errors.\n",errorcount);
	fclose(debugfile);
#endif


}


/*
 * The memmem() function finds the start of the first occurrence of the
 * substring 'needle' of length 'nlen' in the memory area 'haystack' of
 * length 'hlen'.
 *
 * The return value is a pointer to the beginning of the sub-string, or
 * NULL if the substring is not found.
 */
void *memmem(unsigned char *haystack, size_t hlen, unsigned char *needle, size_t nlen)
{
    int needle_first;
    unsigned char *p = haystack;
    size_t plen = hlen;

#if DEBUG_MEMMEM
printf("memmem *hs=%u hlen=%lu nlen=%u\n",(unsigned long)(haystack-inputbuffer),hlen,nlen);
printf("  returned ");
#endif
    if (!nlen)
		{
#if DEBUG_MEMMEM
printf("  NULL because of !nlen\n");
#endif
		return NULL;
		}

	needle_first = *(unsigned char *)needle;

	while (plen >= nlen && (p = memchr(p, needle_first, plen - nlen + 1)))
		{
		if (!memcmp(p, needle, nlen))
			{
#if DEBUG_MEMMEM
printf("  pointer p=%lu\n",(unsigned long)(p-inputbuffer));
#endif
			return (void *)p;
			}

		p++;
		plen = hlen - (p - haystack);
		}

#if DEBUG_MEMMEM
printf("  NULL because of not found\n");
#endif
	return NULL;
}


void process_xml(unsigned char *foundstartptr,size_t searchresultlen)
{
// incoming:
//	pointer to beginning of an xml <releases=
//	length of the xml block

	size_t n;


// Search through the xml for items and write them to the csvfile as CSV.

// Fields to extract:
//  non-nested fields first:
//    Release ID
//    title
//    released
//    country
//    notes
//    data_quality
//    

// 1.  Release ID
	xmlstartstringlen=strlen(SEARCH_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,SEARCH_START);

	xmlendstringlen=strlen(SEARCH_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,SEARCH_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml search returned NULL\n");
		}
	else
		{
		printf("Found xml string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
  		rel_id=strtoul(foundxmlstringptr+13,NULL,10);
		printf("Found xml search string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		printf("Release ID=%lu\n",rel_id);
		fprintf(csvfile,"\"%u\"",release_id);
		printf("to csvfile:\"%u\"\n",release_id);
//		ch=getchar();
		}

// 2.  title
	xmlstartstringlen=strlen(TITLE_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,TITLE_START);

	xmlendstringlen=strlen(TITLE_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,TITLE_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("ERROR! xml title search returned NULL\n");
		ch=getchar();
		}
	else
		{
		printf("Found xml title string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml title end search returned NULL\n");
			ch=getchar();
			}
		else
			{
			fprintf(csvfile,SEPARATOR);
			n=foundxml2stringptr-foundxmlstringptr-strlen(TITLE_START);
			fprintf(csvfile,"\"%.*s\"",n,foundxmlstringptr+strlen(TITLE_START));
			printf("to csvfile: \"%.*s\"\n",n,foundxmlstringptr+strlen(TITLE_START));
//			ch=getchar();
			}
		}


// 3. released
	xmlstartstringlen=strlen(RELEASED_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,RELEASED_START);

	xmlendstringlen=strlen(RELEASED_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,RELEASED_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml released search returned NULL\n");
		fprintf(csvfile,SEPARATOR);
		fprintf(csvfile,"%s",EMPTY_FIELD);
		}
	else
		{
		printf("Found xml released string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml released end search returned NULL\n");
			ch=getchar();
			}
		else
			{
			fprintf(csvfile,SEPARATOR);
			n=foundxml2stringptr-foundxmlstringptr-strlen(RELEASED_START);
			fprintf(csvfile,"\"%.*s\"",n,foundxmlstringptr+strlen(RELEASED_START));
			printf("to csvfile: \"%.*s\"\n",n,foundxmlstringptr+strlen(RELEASED_START));
//			ch=getchar();
			}
		}

// 4. country

	xmlstartstringlen=strlen(COUNTRY_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,COUNTRY_START);

	xmlendstringlen=strlen(COUNTRY_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,COUNTRY_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml country search returned NULL\n");
		fprintf(csvfile,SEPARATOR);
		fprintf(csvfile,"%s",EMPTY_FIELD);
		}
	else
		{
		printf("Found xml country string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml country end search returned NULL\n");
			ch=getchar();
			}
		else
			{
			fprintf(csvfile,SEPARATOR);
			n=foundxml2stringptr-foundxmlstringptr-strlen(COUNTRY_START);
			fprintf(csvfile,"\"%.*s\"",n,foundxmlstringptr+strlen(COUNTRY_START));
			printf("to csvfile: \"%.*s\"\n",n,foundxmlstringptr+strlen(COUNTRY_START));
//			ch=getchar();
			}
		}

// 5. notes

	xmlstartstringlen=strlen(NOTES_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,NOTES_START);

	xmlendstringlen=strlen(NOTES_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,NOTES_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml notes search returned NULL\n");
		fprintf(csvfile,SEPARATOR);
		fprintf(csvfile,"%s",EMPTY_FIELD);
		}
	else
		{
		printf("Found xml notes string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml notes end search returned NULL\n");
			ch=getchar();
			}
		else
			{
			fprintf(csvfile,SEPARATOR);
			n=foundxml2stringptr-foundxmlstringptr-strlen(NOTES_START);
			fprintf(csvfile,"\"%.*s\"",n,foundxmlstringptr+strlen(NOTES_START));
			printf("to csvfile: \"%.*s\"\n",n,foundxmlstringptr+strlen(NOTES_START));
//			ch=getchar();
			}
		}


// 6. data_quality
	xmlstartstringlen=strlen(DATA_QUALITY_START);
	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,DATA_QUALITY_START);

	xmlendstringlen=strlen(DATA_QUALITY_END);
	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,DATA_QUALITY_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml data_quality search returned NULL\n");
		fprintf(csvfile,SEPARATOR);
		fprintf(csvfile,"%s",EMPTY_FIELD);
		}
	else
		{
		printf("Found xml data_quality string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml data_quality end search returned NULL\n");
			fprintf(csvfile,SEPARATOR);
			fprintf(csvfile,"%s",EMPTY_FIELD);
			ch=getchar();
			}
		else
			{
			n=foundxml2stringptr-foundxmlstringptr-strlen(DATA_QUALITY_START);
			fprintf(csvfile,SEPARATOR);
 			fprintf(csvfile,"\"%.*s\"",n,foundxmlstringptr+strlen(DATA_QUALITY_START));
			printf("to csvfile: \"%.*s\"\n",n,foundxmlstringptr+strlen(DATA_QUALITY_START));
//			ch=getchar();
			}
		}


// More fields to extract:
//  nested fields next:
//
//  
/*<labels>
<label catno="MBZZ 004-12" id="2176" name="Mo'Bizz Recordings"/>
</labels><extraartists/>

also:
<labels><label catno="74321-78040-2" id="930" name="Logic Records"/>
<label catno="74321-78040-2" id="926736" name="Beyond (3)"/></labels>
*/


// 7. labels
	xmlstartstringlen=strlen(LABELS_START);
//	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,LABELS_START);

	xmlendstringlen=strlen(LABELS_END);
//	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,LABELS_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("xml labels search returned NULL\n");
		fprintf(csvfile,SEPARATOR);
		fprintf(csvfile,"%s",EMPTY_FIELD);
		}
	else
		{
		printf("Found xml labels string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml labels end search returned NULL\n");
			fprintf(debugfile,"Error! xml labels end search returned NULL\n");
			ch=getchar();
			}
		else
			{
			n=foundxml2stringptr-foundxmlstringptr-strlen(LABELS_START);  // length of one or more <label> entries
			sprintf(tempbuffer,"%.*s\"\0",n,foundxmlstringptr+strlen(LABELS_START));
//			printf("tempbuffer=%.*s\"\n",n,foundxmlstringptr+strlen(LABELS_START));
//			printf(" len=%u ",n);
//			ch=getchar();

//Extract catalog numbers, and label names...
//<labels><label catno="74321-78040-2" id="930" name="Logic Records"/>
//<label catno="74321-78040-2" id="926736" name="Beyond (3)"/></labels>

			catno_count=0;
			tptr=strstr(tempbuffer,"label catno=\"");
			if (tptr==NULL)
				{
				printf("Error!  No label data found.\n");
				ch=getchar();
				}
			while (tptr!=NULL)
				{
				currentptr=strstr(tptr+strlen("label catno=\""),"\"");
//				currentptr=strstr(tptr+1,"\"");
				printf("tptr=%s\n",tptr);
				printf("currentptr=%s\n",currentptr);
				nx=currentptr-tptr-strlen("label catno=\"");
//				printf("nx=%u\n",nx);

				sprintf(catno[catno_count],"%.*s\0",nx,tptr+strlen("label catno=\""));
				printf("catno[%u]=%.*s\0",catno_count,nx,tptr+strlen("label catno=\""));
//				printf("catno[%u]=%.*s\0",catno_count,nx,tptr);

				tptr=strstr(currentptr,"label catno=\"");  // for next iteration

// extract label name
				labelnameptr=strstr(currentptr,"name=\"");
				labelnameptr+=strlen("name=\"");
				nx=strstr(labelnameptr,"\"")-labelnameptr;
//				printf("nx=%u\n",nx);
				sprintf(labelname[catno_count],"%.*s\0",nx,labelnameptr);
				printf("labelname[%u]=%.*s\0",catno_count,nx,labelnameptr);
//strip " (3)" from labelname if present
				xptr=strstr(labelname[catno_count]," (");
				if (xptr!=NULL)
					{
					printf("found parenthetical in label name %s - removing\n",labelname[catno_count]);
					*xptr='\0';
					ch=getchar();
					}
				catno_count++;
//				ch=getchar();
				}
//put label and catno data in csvfile as label--catno.  (defined constant, Later, use &ndash;).
//Into columns:
// export first label, first catno, firstlabel--firstcatno, then all of them in a column. (4 output columns total)
			fprintf(csvfile,SEPARATOR);

			printf("to csvfile: \"%s%s%s\"\n",labelname[0],LABEL_CATNO_SEPARATOR,catno[0]);
			fprintf(csvfile,"\"%s%s%s\"",labelname[0],LABEL_CATNO_SEPARATOR,catno[0]);
			fprintf(csvfile,"%s",SEPARATOR);

			printf("to csvfile: \"%s\"\n",labelname[0]);
			fprintf(csvfile,"\"%s\"",labelname[0]);

			fprintf(csvfile,"%s",SEPARATOR);
			printf("to csvfile: \"%s\"\n",catno[0]);
			fprintf(csvfile,"\"%s\"",catno[0]);

			fprintf(csvfile,"%s",SEPARATOR);
			fprintf(csvfile,"\""); // start field for label+catno list

			for (i=0;i<catno_count;i++)
				{
				printf("to csvfile: \"%s%s%s\"\n",labelname[i],LABEL_CATNO_SEPARATOR,catno[i]);
				fprintf(csvfile,"%s%s%s",labelname[i],LABEL_CATNO_SEPARATOR,catno[i]);
				if (i!=catno_count-1)
					{
					fprintf(csvfile,", ");
					}
				}

			fprintf(csvfile,"\""); // end field for label+catno list

//			ch=getchar();
			}
		}


/*<formats>
<format name="Vinyl" qty="1" text="">
<descriptions><description>12"</description><description>45
RPM</description></descriptions></format>
</formats>
*/


// 8. format
	xmlstartstringlen=strlen(FORMAT_NAME_START);
//	printf("xmlstartringlen=%u\n",xmlstartstringlen);
	strcpy(xmlfindstartbuffer,FORMAT_NAME_START);

	xmlendstringlen=strlen(FORMATS_END);
//	printf("xmlendstringlen=%u\n",xmlendstringlen);
	strcpy(xmlfindendbuffer,FORMATS_END);

	foundxmlstringptr=memmem(foundstartptr, searchresultlen , xmlfindstartbuffer, xmlstartstringlen);
	if (foundxmlstringptr==NULL)
		{
		printf("ERROR! xml format name search returned NULL\n");
		ch=getchar();
		}
	else
		{
		printf("Found format name string within bounds at position %u\n",(foundxmlstringptr-inputbuffer));
		foundxml2stringptr=memmem(foundstartptr, searchresultlen , xmlfindendbuffer, xmlendstringlen);
		if (foundxml2stringptr==NULL)
			{
			printf("Error! xml format name end search returned NULL\n");
			fprintf(debugfile,"Error! xml format name end search returned NULL\n");
			ch=getchar();
			}
		else
			{
// copy block to search later for <descriptions> into tempbuffer
			n=foundxml2stringptr-foundxmlstringptr-strlen(FORMAT_NAME_START);  // length of block containing format <description>s
			sprintf(tempbuffer,"%.*s\"\0",n,foundxmlstringptr+strlen(FORMAT_NAME_START));
			printf("tempbuffer=%.*s\"\n",n,foundxmlstringptr+strlen(FORMAT_NAME_START));

//Extract format fields...
//<format name="Vinyl" qty="1" text="">
//extract format name
			xptr=strstr(tempbuffer,"\"");
			nx=xptr-tempbuffer;
			sprintf(format_name,"%.*s\0",nx,tempbuffer);
			printf("nx=%u format_name,%.*s\n",nx,nx,tempbuffer);

//			ch=getchar();


//extract qty
			currentptr=strstr(tempbuffer,"qty=\"")+strlen("qty=\"");
			tptr=strstr(currentptr,"\"");
			nx=tptr-currentptr;
			sprintf(format_qty,"%.*s\0",nx,currentptr);
			printf("nx=%u format_qty:%.*s\n",nx,nx,currentptr);

			ch=getchar();
//extract text
			currentptr=strstr(tempbuffer,"text=\"")+strlen("text=\"");
			tptr=strstr(currentptr,"\"");
			nx=tptr-currentptr;
			sprintf(format_text,"%.*s\0",nx,currentptr);
			printf("nx=%u format_text:%.*s\n",nx,nx,currentptr);

//			ch=getchar();


// extract <description>fields from tempbuffer
//<descriptions><description>12"</description><description>45
//RPM</description></descriptions></format>

			format_desc_count=0;
			tptr=strstr(tempbuffer,"<description>");
			if (tptr==NULL)
				{
				printf("Error!  No <description> data found.\n");
				ch=getchar();
				}
			while (tptr!=NULL)
				{
				currentptr=strstr(tptr+strlen("<description>"),"</description>");
				printf("tptr=%s\n",tptr);
				printf("currentptr=%s\n",currentptr);
				nx=currentptr-tptr-strlen("<description>");
//				printf("nx=%u\n",nx);

				sprintf(description[format_desc_count],"%.*s\0",nx,tptr+strlen("<description>"));
				printf("description[%u]=%.*s\0",format_desc_count,nx,tptr+strlen("<description>"));
				format_desc_count++;

				tptr=strstr(currentptr,"<description");  // for next iteration

				ch=getchar();
				}

//put format and <description> data in outfile as
// columns: "format_name", "format_qty", "format_text", "description[format_desc_count]"
// then a combined version all in one column.
// export format--
			fprintf(csvfile,"%s",SEPARATOR);

			printf("to csvfile: \"%s\"\n",format_name);
			fprintf(csvfile,"\"%s\"",format_name);
			fprintf(csvfile,"%s",SEPARATOR);

			printf("to csvfile: \"%s\"\n",format_qty);
			fprintf(csvfile,"\"%s\"",format_qty);
			fprintf(csvfile,"%s",SEPARATOR);

			printf("to csvfile: \"%s\"\n",format_text);
			fprintf(csvfile,"\"%s\"",format_text);
			fprintf(csvfile,"%s",SEPARATOR);

			fprintf(csvfile,"\""); // start field for label+catno list
			for (i=0;i<format_desc_count;i++)
				{
				printf("to csvfile: \"%s\"\n",description[i]);
				fprintf(csvfile,"%s",description[i]);
				if (i!=format_desc_count-1)
					{
					fprintf(csvfile,FORMAT_DESCRIPTION_SEPARATOR);
					}
				}

			fprintf(csvfile,"\""); // end field for <description> list

// Now the combined_description version all in one column.
// "format_qty"x"format_text", description[format_desc_count]"
// "2xCD, Reissue, Limited Edition" etc.

			fprintf(csvfile,SEPARATOR);

			if (!strcmp(format_qty,"1"))
				{
				printf("to csvfile: %s\n",format_name);
				fprintf(csvfile,"\"%s",format_name);
				}
			else
				{
				printf("to csvfile: \"%sx%s\n",format_qty,format_name);
				fprintf(csvfile,"\"%sx%s",format_qty,format_name);
				}


			fprintf(csvfile,"%s",FORMAT_DESCRIPTION_SEPARATOR);
			printf("%s",FORMAT_DESCRIPTION_SEPARATOR);
			for (i=0;i<format_desc_count;i++)
				{
				printf("to csvfile: \"%s\"\n",description[i]);
				fprintf(csvfile,"%s",description[i]);
				if (i!=format_desc_count-1)
					{
					fprintf(csvfile,FORMAT_DESCRIPTION_SEPARATOR);
					}
				}

			fprintf(csvfile,"\""); // end field for <description> list

			ch=getchar();


			}
		}




/*
<master_id
is_main_release="false">142262</master_id><tracklist><track><position>A1</position><title>Hot
Shot '97 (Rollercoaster Club
Mix)</title><duration>5:57</duration><extraartists><artist><id>16019</id><name>Rollercoaster</name><anv></anv><join></join><role>Remix</role><tracks></tracks></artist></extraartists></track><track><position>A2</position><title>Hot
Shot '97 (Da Techno Bohemian Hot Vocal
Mix)</title><duration>6:09</duration><extraartists><artist><id>20922</id><name>Da
Techno
Bohemian</name><anv></anv><join></join><role>Remix</role><tracks></tracks></artist></extraartists></track><track><position>B1</position><title>Hot
Shot '97 (Da Techno Bohemian Dirty Dub
Mix)</title><duration>5:25</duration><extraartists><artist><id>20922</id><name>Da
Techno
Bohemian</name><anv></anv><join></join><role>Remix</role><tracks></tracks></artist></extraartists></track><track><position>B2</position><title>Hotdisco</title><duration>4:11</duration></track></tracklist>
*/


		fprintf(csvfile,"\n");

}


