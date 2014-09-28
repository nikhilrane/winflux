/***********************************************************************
************************************************************************


	WINFLUX: FILE SYSTEMS IN WINDOWS USERSPACE – CHALLENGING THE STEREOTYPE!

	DEVELOPERS	:	1.	PANKAJ LAGU
					2.	PRASAD PHADNIS
					3.	SUMITRA MADHAV
					4.	NIKHIL RANE


***********************************************************************
***********************************************************************/




#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "dokan.h"
#include <winioctl.h>
#include <string.h>
#include <conio.h>
#include <tchar.h>
#include <process.h>
#include <locale.h>
#include "fileinfo.h"


#include "ext2fs.h"					// this file contains standard Ext2 structures as defined in Linux




struct c_queue{						//circular queue to implement the TLB concept for caching directory listings...
	char path[EXT2_NAME_LEN];
	__u32 inode;
}queue[128];

struct node{						//singly linked list to store the directory nodes which are not yet traversed...
	char dir_name[EXT2_NAME_LEN];
	struct node *next;
};



int root=0;							//variable to keep track if root is opened or not...
int front=0,rear=0;					
struct ext2_super_block *sb_ptr;
static WCHAR RootDirectory[MAX_PATH] = L"C:";




/***************************************************************************************


					GETFILEPATH MODULE

		INPUT   : FILE NAME & FILEPATH
		OUTPUT  : FILEPATH WILL ALWAYS CONTAIN THE ROOT DIRECTORY 
				  i.e. THE MOUNTED PARTITION
		PROCESS : SINCE WINDOWS CANNOT RETURN HANDLE TO THE FILES
				  WE STORE ROOT DIRECTORY AS THE FILE PATH EVERY SINGLE TIME
				  SO AS TO OBTAIN THE HANDLE TO THE VOLUME.


**************************************************************************************/




static void
GetFilePath(
	PWCHAR	filePath,
	LPCWSTR FileName)
{
	RtlZeroMemory(filePath, MAX_PATH);
	wcsncpy(filePath,
			TEXT("\\\\.\\"),
			7);
	wcsncat(filePath,
			RootDirectory,
			wcslen(RootDirectory));
}




/***************************************************************************************


					CHECK_IN_QUEUE MODULE

		INPUT   : FILE NAME
		OUTPUT  : INODE NUMBER CORRESPONDING TO THE INPUT FILENAME
		PROCESS : SEARCH THE CIRCULAR QUEUE FOR THE INODE NUMBER BASED 
				  ON THE INPUT FILENAME


**************************************************************************************/




int	Check_In_Queue(LPCWSTR FileName)
{
	  int temp_loop=0,temp_inode=-1;								//loop variable
	  while(temp_loop < 128)										//loop for the queue
	  {
		  if( !wcscmp(queue[temp_loop].path,FileName) )				//search for the inode of the path
		  {
			temp_inode=queue[temp_loop].inode;
			break;
		  }
		  temp_loop++;
	  }
	  return temp_inode;
}



/***************************************************************************************


					STRING_COMPARE MODULE

		INPUT   : TWO STRINGS OF wchar_t DATA TYPE
		OUTPUT  :  0  IF THE STRINGS ARE MATCHING
				  -1  IF LENGTH IS EQUAL BUT STRINGS ARE NOT MATCHING
				  'N' THE DIFFERENCE IN LENGTHS OF THE STRINGS, WHEN
				      THE LENGTH IS ALSO DIFFERENT
		PROCESS : CHECK IF THE LENGTH IS SAME AND IF SO THEN CHECK CHARACTER BY
				  CHARACTER IF STRINGS MATCH, RETURN PROPER VALUE ACCORDINGLY.


**************************************************************************************/



int string_compare(
				   wchar_t name[EXT2_NAME_LEN],
				   wchar_t Path[EXT2_NAME_LEN])
{
	int j;

	if(wcslen(name) == wcslen(Path))		// IF LENGTH IS SAME
	{
		for(j=0; name[j]!='\0'; j++)
		{
			if(name[j]==Path[j])
				continue;
			else
				break;
		}
		if(j==wcslen(name))
		{

			return 0;
		}
		else 
			return -1;
	}
	else
		return (wcslen(name) - wcslen(Path));

}





/***************************************************************************************


					XTDIV MODULES

		INPUT   : NUMERATOR AND DENOMINATOR
		OUTPUT  : RETURN QUOTIENT
		PROCESS : CALCULATE NUMERATOR / DENOMINATOR BUT 
				  XTDIV2 --> ROUNDS IT UP
				  XTDIV1,XTDIV64 --> RETURNS WHOLE PART & REMAINDER


**************************************************************************************/




__u32 XtDiv2(
	__u32 Neumerator,
	__u32 Denominator
)
{
	__u32 Result = Neumerator / Denominator;
	if((Neumerator % Denominator) > 0)
	{
		Result++;
	}
	return Result;
}





__u32 XtDiv1(
	__u32 Neumerator,
	__u32 Denominator,
	__u32 *Remainder
)
{
	__u32 Result = Neumerator / Denominator;
	if(Remainder)
	{
		*Remainder = Neumerator % Denominator;
	}
	return Result;
}





__u32 XtDiv64(
	ULONGLONG Neumerator,
	__u32 Denominator,
	__u32 *Remainder
)
{
	__u32 Result = (__u32)(Neumerator / Denominator);
	if(Remainder)
	{
		*Remainder = (__u32)(Neumerator % (ULONGLONG)Denominator);
	}
	return Result;
}




/***************************************************************************************

				
						FETCHPTRVAL MODULE

		INPUT   : HANDLE TO THE PARTITION , BLOCKNO , BLOCKID
		OUTPUT  : BLOCKNO
		PROCESS : USING THE INPUT HANDLE GO TO THE INPUT BLOCKNO. THIS
				  BLOCK WILL CONTAIN A LIST OF BLOCK POINTERS.  ACCESS
				  THE POINTER INDEXED BY BLOCKID. RETRIEVE THE BLOCKNO
				  IT IS POINTING TO.


**************************************************************************************/




__u32 FetchPtrVal(
					HANDLE	gd_handle,
					__u32	blockno,
					__u32	blockid
				  )
{
	LARGE_INTEGER	i;
	char			buffer[4096];
	__u32			*blockno_ptr=malloc(sizeof(__u32));
	BOOL			bResult;
	DWORD			rt;
	
					i.LowPart=4096*blockno;
					i.HighPart=0;
					bResult=SetFilePointerEx(
												gd_handle,
												i,
												NULL, 
												0
											);

					bResult=ReadFile(
										gd_handle,
										(LPVOID)buffer,4096,
										&rt,
										NULL
									);

					memcpy(blockno_ptr,buffer+(blockid)*4,4);    
					return *blockno_ptr;
					
}





/***************************************************************************************


					READ_SUPERBLOCK MODULE

		INPUT   : VOID
		OUTPUT  : VOID
		PROCESS : ACCESS THE SUPERBLOCK RESIDING ON THE MOUNTED PARTITION AND THEN 
				  INITIALISE THE SB_PTR AND OTHER PARAMETERS THAT ARE SPECIFIC TO 
				  THE MOUNTED PARTITION.


**************************************************************************************/





void
Read_Superblock()
{
	HANDLE	sb_handle;
	WCHAR	filePath[MAX_PATH];
	LARGE_INTEGER i;
	BOOL bResult=FALSE;
	DWORD wmWritten=0;

	GetFilePath(filePath,"");
	sb_ptr=malloc(sizeof(struct ext2_super_block));
															//Create handle for the device
    sb_handle = CreateFile(filePath,						// drive to open   
							GENERIC_READ,					// no access to the drive
							FILE_SHARE_READ |				// share mode
							FILE_SHARE_WRITE, 
				            NULL,							// default security attributes
						    OPEN_EXISTING,					// disposition
							0,								// file attributes
							NULL);							// do not copy file attributes


	if (sb_handle != INVALID_HANDLE_VALUE)					// cannot open the drive
	{
		  i.LowPart=1024;
		  i.HighPart=0;
		  
		  bResult=SetFilePointerEx(
						sb_handle,
						i,
						NULL,
						0);
		  
		  bResult=ReadFile(
					sb_handle,
					(LPVOID)sb_ptr,
					1024,
					&wmWritten,
					NULL);
	}
}






/***************************************************************************************


						WINFLUX_GETDISKFREESPACE MODULE

		IN-OUT  : FREEBYTESAVAILABLE , TOTALNUMBEROFBYTES , TOTALNUMBEROFFREEBYTES
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : READ THE SUPERBLOCK AND THEN SET THE TOTALNUMBEROFBYTES AND 
				  TOTALNUMBEROFFREEBYTES ACCORDINGLY DEPENDING ON THE BLOCK SIZE 
				  ON THE PARTITION. THIS INFORMATION IS ACQUIRED FROM THE SUPER-
				  BLOCK.


**************************************************************************************/





int
WinFlux_GetDiskFreeSpace(
	PULONGLONG			FreeBytesAvailable,
	PULONGLONG			TotalNumberOfBytes,
	PULONGLONG			TotalNumberOfFreeBytes,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	Read_Superblock();
	*TotalNumberOfBytes = sb_ptr->s_blocks_count * (EXT2_BLOCK_SIZE(sb_ptr));
	*FreeBytesAvailable = sb_ptr->s_free_blocks_count * (EXT2_BLOCK_SIZE(sb_ptr));
	*TotalNumberOfFreeBytes = 512*1024*1024;
	return 0;
}




/***************************************************************************************


						WINFLUX_GETVOLUMEINFORMATION MODULE

		IN-OUT  : VOLUMENAMEBUFFER , VOLUMENAMESIZE , VOLUMESERIALNUMBER , 
				  MAXCOMPONENTLEN , FILESYSTEMFLAGS , FILESYSTEMNAMEBUFFER, 
				  FILESYSTEMNAMESIZE , DOKANFILEINFO
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : SET THE VOLUMENAMEBUFFER , VOLUMESERIALNUMBER , MAXCOMPONENTLEN
				  AND FILESYSTEMFLAGS. ALSO SET THE FILESYSTEMNAMEBUFFER TO EXT3.


**************************************************************************************/




int
WinFlux_GetVolumeInformation(
	LPWSTR		VolumeNameBuffer,
	DWORD		VolumeNameSize,
	LPDWORD		VolumeSerialNumber,
	LPDWORD		MaximumComponentLength,
	LPDWORD		FileSystemFlags,
	LPWSTR		FileSystemNameBuffer,
	DWORD		FileSystemNameSize,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	wcscat(VolumeNameBuffer,L"WinFlux");
	*VolumeSerialNumber = 0x19831116;
	*MaximumComponentLength = 256;
	*FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | 
						FILE_CASE_PRESERVED_NAMES | 
						FILE_SUPPORTS_REMOTE_STORAGE |
						FILE_UNICODE_ON_DISK;

	wcscat(FileSystemNameBuffer, L"Ext3");
	return 0;
}






/***************************************************************************************


						INSERT_INTO_NODE_LIST MODULE

		INPUT   : POINTER TO HEAD OF LINKED LIST AND DIRECTORY NAME
		OUTPUT  : VOID
		PROCESS : CREATE A NODE CONTAINING THE INPUT DIRECTORY NAME
				  AND INSERT IT AT THE START OF THE LINKED LIST.


**************************************************************************************/




void
insert_into_node_list(
	  struct	node **head,
	  char		dir_name[])
{
	struct node *p=NULL;

	strcpy(p->dir_name,dir_name);			//copy directory name into the node data
	p->next=NULL;

	if(*head==NULL)							//if linked list is empty
		*head=p;
	else									//if linked is created, insert node to the start of the LL and update head pointer
	{
		p->next=*head;
		*head=p;
	}
}





/***************************************************************************************


						FIND_INODE_INFO MODULE

		INPUT   : INODE NUMBER 
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : GET THE INODE CORRESPONDING TO THE INPUT INODE NUMBER AND RETURN 
				  THE POINTER POINTING TO IT.
					

**************************************************************************************/

	



int
Find_Inode_Info(
	   int temp_inode,
	   struct ext2_inode **inode_ptr)
{
	HANDLE gd_handle;
	struct ext2_group_desc *gd_ptr;
	LARGE_INTEGER i;
	int temp_block_group,temp_inode_index;
	DWORD rt=0;
	WCHAR filePath[MAX_PATH];
	BOOL bResult;
	char buffer[4096];

	gd_ptr=malloc(sizeof(struct ext2_group_desc));
	GetFilePath(filePath,"");
    gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ,     // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes

	if (gd_handle == INVALID_HANDLE_VALUE) // cannot open the drive
		return (FALSE);

	//now find appropiate block group..
	temp_block_group=(temp_inode-1) /  sb_ptr->s_inodes_per_group;
	
	//now find appropiate index..
	temp_inode_index=(temp_inode-1) % sb_ptr->s_inodes_per_group;
	
	//set offset..
	i.LowPart=4096;
	i.HighPart=0;
	bResult=SetFilePointerEx(
								gd_handle,
								i,
								NULL, 
								0
							);

	bResult=ReadFile(
			gd_handle,
			(LPVOID)buffer,4096,
			&rt,
			NULL
			);

	// read first entry frm group_desc_table
	memcpy(gd_ptr,buffer+temp_block_group*32,32);    
	i.LowPart=gd_ptr->bg_inode_table*4096;

	//for reading inode table------------------------------
	bResult=SetFilePointerEx(
	gd_handle,
	i,
	NULL, 
	0
	);

	bResult=ReadFile(
		gd_handle,
		(LPVOID)buffer,4096,
		&rt,
		NULL
		);

	memcpy(*inode_ptr,buffer+256*temp_inode_index,256);    	
}





/***************************************************************************************


					  READ_DIRECTORY_LISTING MODULE

		INPUT   : INODE NUMBER 
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : GET THE INODE CORRESPONDING TO THE INPUT INODE NUMBER AND RETURN 
				  THE POINTER POINTING TO IT.
					

**************************************************************************************/





int
Read_Directory_Listing(
		int					temp_inode,
		LPCWSTR				FileName,
		PFillFindData		FillFindData, // function pointer
		PDOKAN_FILE_INFO	DokanFileInfo)
{
	
  WIN32_FIND_DATAW	findData;
  WCHAR				filePath[MAX_PATH];
  HANDLE			gd_handle;               // handle to the drive to be examined 
  BOOL				bResult;                 // results flag
  
  struct ext2_group_desc *gd_ptr;
  struct ext2_inode *inode_table_ptr,*inode_ptr;
  struct ext2_dir_entry_2 *dir_ptr;
  char buffer[4096];
  int temp_block_group,temp_inode_index,temp_counter=0,dot_dotdot_check=0,temp_length=0;

  DWORD wmWritten=0,rt=0; 
  LARGE_INTEGER i;
  int j=0,l=0;
  

  gd_ptr=malloc(sizeof(struct ext2_group_desc)); 
  inode_table_ptr=malloc(256);
  inode_ptr=malloc(256);
  dir_ptr=malloc(sizeof(struct ext2_dir_entry_2));


  GetFilePath(filePath, FileName);
  Read_Superblock();

 //Create handle for the device
    gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ,                // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes

	if (gd_handle == INVALID_HANDLE_VALUE) // cannot open the drive
      return (FALSE);
     
	//seek to read from 4096
	i.LowPart=4096;
	i.HighPart=0;

	bResult=SetFilePointerEx(
				gd_handle,
				i,
				NULL,
				0);

	bResult=ReadFile(
				gd_handle,
				(LPVOID)buffer,4096,
				&rt,
				NULL);


	//common part ends..starting for root listing...
	if(!temp_inode)
	{
		  memcpy(gd_ptr,buffer,32); 
		  i.LowPart=gd_ptr->bg_inode_table * 4096;

		  //for reading inode table of root directory
		  bResult=SetFilePointerEx(
					  gd_handle,
					  i,
					  NULL,
					  0);

		  bResult=ReadFile(
					  gd_handle,
					  (LPVOID)buffer,
					  512, 
					  &rt,
					  NULL);


			memcpy(inode_table_ptr,buffer+256,256);    
			 

			//seeking inode_table_ptr->i_block[0]
			i.LowPart=inode_table_ptr->i_block[0] * 4096;

			bResult=SetFilePointerEx(
								gd_handle,
								i,
								NULL, 
								0);
			
			bResult=ReadFile(
						gd_handle,
						(LPVOID)buffer,
						4096,
						&rt,
						NULL);
			root=1;
	}	    
	else												//for a directory other than the root
	{	
			//find appropiate block group..
			temp_block_group=(temp_inode-1) / sb_ptr->s_inodes_per_group;
				
			//now find appropiate index..
			temp_inode_index=(temp_inode-1) % sb_ptr->s_inodes_per_group;
		
			// read first entry frm group_desc_table
			memcpy(gd_ptr,buffer+temp_block_group*32,32);    
		
			i.LowPart=gd_ptr->bg_inode_table*4096;

			//for reading inode table------------------------------
			bResult=SetFilePointerEx(
						gd_handle,
						i,
						NULL,
						0);

			bResult=ReadFile(
				gd_handle,
				(LPVOID)buffer,4096,
				&rt,
				NULL
				);

			memcpy(inode_table_ptr,buffer+256*temp_inode_index,256);    
						
			// seeking inode_table_ptr->i_block[0]
			//for reading inode table------------------------------

			i.LowPart=inode_table_ptr->i_block[0]*4096;
			
			bResult=SetFilePointerEx(
						gd_handle,
						i,
						NULL, 
						0);

			bResult=ReadFile(
					gd_handle,
					(LPVOID)buffer,4096,
					&rt,
					NULL);
	}//end of else part for root=false


			dot_dotdot_check=0;
			j=0;
			do
			{
				memcpy(dir_ptr,buffer+j,sizeof(struct ext2_dir_entry_2));    
				
				if(dir_ptr->inode==0 || (dir_ptr->rec_len==0 && dir_ptr->name_len==0) )		//if objects in current directory are over,break!
					break;
				
				dot_dotdot_check++;					//to keep track of non-listing of '.' and '..' entries

						
				if(dir_ptr->file_type==2)			//set the attribute to directory if its a directory
				{
					findData.dwFileAttributes=16;
					DokanFileInfo->IsDirectory = TRUE;
				}
				else
				{
					findData.dwFileAttributes=32;	//else set to file
					DokanFileInfo->IsDirectory = FALSE;
					Find_Inode_Info(dir_ptr->inode, &inode_ptr);
					findData.nFileSizeLow = inode_ptr->i_size;
					findData.nFileSizeHigh = 0;
				}
			
				if(dot_dotdot_check > 2)
				{
				   char temp_string[EXT2_NAME_LEN];

				   for(l=0;l<EXT2_NAME_LEN;l++)
					   temp_string[l]='\0';

				   for(l=0;l<dir_ptr->name_len;l++)	//copy object name in the findData.name structure
					 findData.cFileName[l]=dir_ptr->name[l];
				
				   findData.cFileName[l]='\0';

				  
				   FillFindData(&findData, DokanFileInfo);

				   wcsncat(temp_string,FileName,wcslen(FileName));
				  

				   if( wcslen(FileName) >= 2 )
					  wcsncat(temp_string,TEXT("\\"),2);

				   wcsncat(temp_string,findData.cFileName, wcslen(findData.cFileName) );
				   
				   if( Check_In_Queue(temp_string) == -1 )
				   {
				       wcsncpy(queue[rear].path, temp_string, wcslen(temp_string) );			//initialize the circular queue implementing TLB concept
				       queue[rear].inode=dir_ptr->inode;
					   rear= (rear+1) % 128;
				   }
				   temp_string[0]='\0';
				}

				j+=dir_ptr->rec_len;
			}while(1);
			
	return 0;
}




/***************************************************************************************


					  READ_LISTING_FOR_CHECK MODULE

		INPUT   : INODE NUMBER OF PARENT , FILENAME , FLAG
		OUTPUT  : INODE NUMBER CORRESPONDING TO FILENAME
		PROCESS : CHECK DIRECTORY LISTING OF PARENT INODE AND THEN ADD THE CONTENTS OF
				  DIRECTORY LISTING TO THE "TLB" STRUCTURE. RETURN INODE NUMBER OF INPUT
				  FILENAME.
					

**************************************************************************************/





int
Read_Listing_For_Check(
		int					temp_inode,
		wchar_t				FileName[EXT2_NAME_LEN],	
		wchar_t				Object_Name[EXT2_NAME_LEN],
		int					flag)
{
	
  WCHAR				filePath[EXT2_NAME_LEN];
  HANDLE			gd_handle;               // handle to the drive to be examined 
  BOOL				bResult;                 // results flag
  
  struct ext2_group_desc *gd_ptr;
  struct ext2_inode *inode_table_ptr,*inode_ptr;
  struct ext2_dir_entry_2 *dir_ptr;
  char buffer[4096];
  int temp_block_group,temp_inode_index,temp_counter=0,dot_dotdot_check=0,temp_length=0;

  DWORD wmWritten=0,rt=0; 
  LARGE_INTEGER i;
  int j=0,l=0;
  

  gd_ptr=malloc(sizeof(struct ext2_group_desc)); 
  inode_table_ptr=malloc(256);
  inode_ptr=malloc(256);
  dir_ptr=malloc(sizeof(struct ext2_dir_entry_2));


  //GetFilePath(filePath, FileName);
  Read_Superblock();


  //Create handle for the device
    gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ,                // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes

	if (gd_handle == INVALID_HANDLE_VALUE) // cannot open the drive
      return (FALSE);
     
	//seek to read from 4096
	i.LowPart=4096;
	i.HighPart=0;

	bResult=SetFilePointerEx(
				gd_handle,
				i,
				NULL,
				0);

	bResult=ReadFile(
				gd_handle,
				(LPVOID)buffer,4096,
				&rt,
				NULL);


	//common part ends..starting for root listing...
	if(!temp_inode)
	{
		  memcpy(gd_ptr,buffer,32); 
		  i.LowPart=gd_ptr->bg_inode_table * 4096;

		  //for reading inode table of root directory
		  bResult=SetFilePointerEx(
					  gd_handle,
					  i,
					  NULL,
					  0);

		  bResult=ReadFile(
					  gd_handle,
					  (LPVOID)buffer,
					  512, 
					  &rt,
					  NULL);


			memcpy(inode_table_ptr,buffer+256,256);    
			 

			//seeking inode_table_ptr->i_block[0]
			i.LowPart=inode_table_ptr->i_block[0] * 4096;

			bResult=SetFilePointerEx(
								gd_handle,
								i,
								NULL, 
								0);
			
			bResult=ReadFile(
						gd_handle,
						(LPVOID)buffer,
						4096,
						&rt,
						NULL);
			//root=1;
	}	    
	else												//for a directory other than the root
	{	
			//find appropiate block group..
			temp_block_group=(temp_inode-1) / sb_ptr->s_inodes_per_group;
				
			//now find appropiate index..
			temp_inode_index=(temp_inode-1) % sb_ptr->s_inodes_per_group;
		
			// read first entry frm group_desc_table
			memcpy(gd_ptr,buffer+temp_block_group*32,32);    
		
			i.LowPart=gd_ptr->bg_inode_table*4096;

			//for reading inode table------------------------------
			bResult=SetFilePointerEx(
						gd_handle,
						i,
						NULL,
						0);

			bResult=ReadFile(
				gd_handle,
				(LPVOID)buffer,4096,
				&rt,
				NULL
				);

			memcpy(inode_table_ptr,buffer+256*temp_inode_index,256);    
						
			// seeking inode_table_ptr->i_block[0]
			//for reading inode table------------------------------

			i.LowPart=inode_table_ptr->i_block[0]*4096;
			
			bResult=SetFilePointerEx(
						gd_handle,
						i,
						NULL, 
						0);

			bResult=ReadFile(
					gd_handle,
					(LPVOID)buffer,4096,
					&rt,
					NULL);
	}//end of else part for root=false


			dot_dotdot_check=0;
			j=0;
			do
			{
				memcpy(dir_ptr,buffer+j,sizeof(struct ext2_dir_entry_2));    
				
				if(dir_ptr->inode==0 || (dir_ptr->rec_len==0 && dir_ptr->name_len==0) )		//if objects in current directory are over,break!
					break;
				
				dot_dotdot_check++;					//to keep track of non-listing of '.' and '..' entries

			
				if(dot_dotdot_check > 2)
				{
				   char temp_string[EXT2_NAME_LEN];

				   for(l=0;l<EXT2_NAME_LEN;l++)
					   temp_string[l]='\0';


				   wcsncat(temp_string,FileName,wcslen(FileName));
				  

				   if( wcslen(FileName) >= 2 )
					  wcsncat(temp_string,TEXT("\\"),1);

				   if( Check_In_Queue(temp_string) == -1 )
				   {
				       wcsncpy(queue[rear].path, temp_string, wcslen(temp_string) );			//initialize the circular queue implementing TLB concept
				       queue[rear].inode=dir_ptr->inode;
					   rear= (rear+1) % 128;
				   }
				   temp_string[0]='\0';
				}

				if( !wcscmp(Object_Name,dir_ptr->name) && flag==1)
					return (dir_ptr->inode);

				j+=dir_ptr->rec_len;
			}while(1);
			
	return -1;
}





/***************************************************************************************


					  CHECK_IF_EXISTS MODULE

		INPUT   : FILEPATH
		OUTPUT  : SUCCESS IF FILE EXISTS
		PROCESS : CHECK WHETHER THE INPUT FILE EXIST BY ACTUALLY TRAVERSING THE INPUT PATH
					

**************************************************************************************/





int Check_If_Exists(
		wchar_t Path[EXT2_NAME_LEN])
{

	wchar_t local_path[EXT2_NAME_LEN],Object_Name[EXT2_NAME_LEN];
	int temp_loop_var,number_of_slashes=0,ptr=0,k=0,j=0, temp_inode=-1;

	for(k=0; k<EXT2_NAME_LEN; k++)
		local_path[k]='\0';

	wcsncpy(local_path,Path,wcslen(Path));


	if (wcslen(Path) > 2)		//path is not root i.e. "\"
	  {
		  int temp_len=wcslen(Path);
		  char ch='\0';
		  ch=local_path[temp_len-1];
		  if(ch=='\\')					//check if last character is a slash '\'
		  {						//if slash is found, return
			  return -100;
		  }
	  }
	

	for(temp_loop_var=(wcslen(Path))-1; Path[temp_loop_var]!='\\';temp_loop_var--);		//traverse till the last slash to extract last object name
	

	for( ptr=0; Path[temp_loop_var]!='\0'; temp_loop_var++,ptr++)							//copy the last object name
	{
		Object_Name[ptr]=Path[temp_loop_var];

	}

	Object_Name[ptr]='\0';

	for(temp_loop_var=0;Path[temp_loop_var]!='\0';temp_loop_var++)
	{
		if(Path[temp_loop_var]=='\\')
			number_of_slashes++;
	}

  if(number_of_slashes==1)		//if root is not yet opened
  {
	  return(Read_Listing_For_Check(0,Path,Object_Name,1));
  }
  else							//if directory under root is accessed
  {
	  temp_inode=Check_In_Queue(Path);


	  if(temp_inode==-1)		//if inode of the path is not found...
	  {
		  wchar_t temp_dir_name[EXT2_NAME_LEN];
		  struct node *head=NULL;
		  head=(struct node *) malloc (sizeof(struct node));


		  do					//traverse the path from end to separate the directories not yet listed and create a LL of these nodes.
		  {


			  for(j= (wcslen(local_path))-1 ;local_path[j]!='\\' ;j--);			//copy the name of last directory

			  for(k=0; k<EXT2_NAME_LEN; k++)
				  temp_dir_name[k]='\0';
				  
			  for(temp_loop_var=j+1,k=0; local_path[temp_loop_var]!='\0'; temp_loop_var++,k++)
			  {
				temp_dir_name[k]=local_path[temp_loop_var];
			  }


			  temp_dir_name[k]=local_path[j]='\0';

			  insert_into_node_list(&head,temp_dir_name);			//insert the directory name into node_list

			  temp_inode=Check_In_Queue(local_path);	//check for the new FileName if its cached

			  
		  }while(temp_inode==-1);

		  temp_inode=Read_Listing_For_Check(temp_inode,local_path,Object_Name,0);		//send current path for directory listing

		  while(head->next!=NULL)				//traverse the LL to append and start directory listing
		  {
			  struct node *p;
			  p=head;
			  wcsncat(local_path,TEXT("\\"),1);		//create new FilePath
			  wcsncat(local_path,p->dir_name,strlen(p->dir_name));

			  temp_inode=Check_In_Queue(local_path);
			  temp_inode=Read_Listing_For_Check(temp_inode,local_path,Object_Name,0);
			  head=head->next;				//proceed to next sub-directory
			  free(p);						//free the traversed node
		  }
		  return(Read_Listing_For_Check(temp_inode,local_path,Object_Name,1));
	  }
	  else
		  return(Read_Listing_For_Check(temp_inode,Path,Object_Name,1));
  }

	return -1;
}



#define WinFlux_CheckFlag(val, flag) if(val&flag) fprintf(stderr, "\t" #flag "\n")





/***************************************************************************************


					  WINFLUX_CREATEFILE MODULE

		INPUT   : FILENAME ALONG WITH THE FLAGS AND ATTRIBUTES
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : CREATEFILE AND SET ITS SECURITY FLAGS & ATTRIBUTES.
					

**************************************************************************************/






static int
WinFlux_CreateFile(
	LPCWSTR					FileName,
	DWORD					AccessMode,
	DWORD					ShareMode,
	DWORD					CreationDisposition,
	DWORD					FlagsAndAttributes,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	WCHAR filePath[MAX_PATH];
	wchar_t local_path[EXT2_NAME_LEN],temp_dir_name[EXT2_NAME_LEN],temp_str[EXT2_NAME_LEN];
	HANDLE handle;
	int j=0,k=0,temp_loop_var=0;



	GetFilePath(filePath, FileName);

	if (CreationDisposition == CREATE_NEW)
		fprintf(stderr, "\tCREATE_NEW\n");
	if (CreationDisposition == OPEN_ALWAYS)
		fprintf(stderr, "\tOPEN_ALWAYS\n");
	if (CreationDisposition == CREATE_ALWAYS)
		fprintf(stderr, "\tCREATE_ALWAYS\n");
	if (CreationDisposition == OPEN_EXISTING)
		fprintf(stderr, "\tOPEN_EXISTING\n");
	if (CreationDisposition == TRUNCATE_EXISTING)
		fprintf(stderr, "\tTRUNCATE_EXISTING\n");

	fprintf(stderr, "\tShareMode = 0x%x\n", ShareMode);

	WinFlux_CheckFlag(ShareMode, FILE_SHARE_READ);
	WinFlux_CheckFlag(ShareMode, FILE_SHARE_WRITE);
	WinFlux_CheckFlag(ShareMode, FILE_SHARE_DELETE);

	fprintf(stderr, "\tAccessMode = 0x%x\n", AccessMode);

	WinFlux_CheckFlag(AccessMode, GENERIC_READ);
	WinFlux_CheckFlag(AccessMode, GENERIC_WRITE);
	WinFlux_CheckFlag(AccessMode, GENERIC_EXECUTE);
	
	WinFlux_CheckFlag(AccessMode, DELETE);
	WinFlux_CheckFlag(AccessMode, FILE_READ_DATA);
	WinFlux_CheckFlag(AccessMode, FILE_READ_ATTRIBUTES);
	WinFlux_CheckFlag(AccessMode, FILE_READ_EA);
	WinFlux_CheckFlag(AccessMode, READ_CONTROL);
	WinFlux_CheckFlag(AccessMode, FILE_WRITE_DATA);
	WinFlux_CheckFlag(AccessMode, FILE_WRITE_ATTRIBUTES);
	WinFlux_CheckFlag(AccessMode, FILE_WRITE_EA);
	WinFlux_CheckFlag(AccessMode, FILE_APPEND_DATA);
	WinFlux_CheckFlag(AccessMode, WRITE_DAC);
	WinFlux_CheckFlag(AccessMode, WRITE_OWNER);
	WinFlux_CheckFlag(AccessMode, SYNCHRONIZE);
	WinFlux_CheckFlag(AccessMode, FILE_EXECUTE);
	WinFlux_CheckFlag(AccessMode, STANDARD_RIGHTS_READ);
	WinFlux_CheckFlag(AccessMode, STANDARD_RIGHTS_WRITE);
	WinFlux_CheckFlag(AccessMode, STANDARD_RIGHTS_EXECUTE);

	fprintf(stderr, "\tFlagsAndAttributes = 0x%x\n", FlagsAndAttributes);

	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_ARCHIVE);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_ENCRYPTED);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_HIDDEN);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_NORMAL);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_OFFLINE);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_READONLY);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_SYSTEM);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_ATTRIBUTE_TEMPORARY);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_WRITE_THROUGH);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_OVERLAPPED);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_NO_BUFFERING);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_RANDOM_ACCESS);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_SEQUENTIAL_SCAN);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_DELETE_ON_CLOSE);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_BACKUP_SEMANTICS);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_POSIX_SEMANTICS);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_OPEN_REPARSE_POINT);
	WinFlux_CheckFlag(FlagsAndAttributes, FILE_FLAG_OPEN_NO_RECALL);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_ANONYMOUS);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_IDENTIFICATION);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_IMPERSONATION);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_DELEGATION);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_CONTEXT_TRACKING);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_EFFECTIVE_ONLY);
	WinFlux_CheckFlag(FlagsAndAttributes, SECURITY_SQOS_PRESENT);

	handle=CreateFile(filePath,  // drive to open   
                    GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE,     // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    FILE_ATTRIBUTE_NORMAL,                // file attributes
                    NULL);            // do not copy file attributes


	if (handle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		return error * -1; // error codes are negated value of Windows System Error codes
	}

	fprintf(stderr, "\n");

	// save the file handle in Context
	DokanFileInfo->Context = (ULONG64)handle;
	return 0;


}







/***************************************************************************************


					  WINFLUX_OPENDIRECTORY MODULE

		INPUT   : FILENAME ALONG DOKANFILEINFO
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : GETS THE HANDLE TO THE DIRECTORY AND STORE IT IN DOKANFILEINFO  
					

**************************************************************************************/







static int
WinFlux_OpenDirectory(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	WCHAR filePath[MAX_PATH];
	HANDLE handle;

	GetFilePath(filePath, FileName);

	handle = CreateFile(
		filePath,
		0,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		return error * -1;
	}


	DokanFileInfo->Context = (ULONG64)handle;

	return 0;
}





static int
WinFlux_CloseFile(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	WCHAR filePath[MAX_PATH];
	GetFilePath(filePath, FileName);

	if (DokanFileInfo->Context) {
		CloseHandle((HANDLE)DokanFileInfo->Context);
		DokanFileInfo->Context = 0;
	} else {
		return 0;
	}

	return 0;
}




static int
WinFlux_Cleanup(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	WCHAR filePath[MAX_PATH];
	GetFilePath(filePath, FileName);

	if (DokanFileInfo->Context) {
		CloseHandle((HANDLE)DokanFileInfo->Context);
		DokanFileInfo->Context = 0;
	
	} else {
		return -1;
	}

	return 0;
}




/***************************************************************************************

					MEMORY ALLOCATION MODULE

		INPUT   : INODE NUMBER
		OUTPUT  : BLOCK NUMBER OF NEWLY RESERVED BLOCK
		PROCESS : CHECKS THE BLOCK BITMAP OF THE BLOCK GROUP TO WHICH THE INPUT INODE 
				  CORRESPONDS TO,THEN FINDS THE FIRST AVAILABLE MEMORY BLOCK.IT THEN 
				  RESERVES THIS BLOCK AND RETURNS THE BLOCK NUMBER OF THIS RECENTLY
				  RESERVED BLOCK.


**************************************************************************************/




int alloc_mem_single_block(int inode_no,WCHAR	filePath[MAX_PATH])
{

	HANDLE gd_handle;
	LARGE_INTEGER	LargeSize,i;
	BOOL bResult;
	DWORD	rt;
	char buffer[4096];
	struct ext2_group_desc *gd_ptr=malloc(sizeof(struct ext2_group_desc)); 
	int k,j,setbit;

	gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ|GENERIC_WRITE,     // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes


  
//seek to read the offset of grp_desc_table  
  i.LowPart=4096;
  i.HighPart=0;

bResult=SetFilePointerEx(
  gd_handle,
  i,
  NULL, 
  1
);

bResult=ReadFile(
		gd_handle,
		(LPVOID)buffer,4096,
		&rt,
		NULL
		);


	memcpy(gd_ptr,buffer,32); //get grp_desc of block 0..actually v have to access grp_dec corresponding to the input inode no..
	printf("success for gd_ptr..inode_table=%d   block_bitmap=%d\n",gd_ptr->bg_inode_table,gd_ptr->bg_block_bitmap);

	i.LowPart=gd_ptr->bg_block_bitmap*4096;

//for reading block bitmap------------------------------
bResult=SetFilePointerEx(
  gd_handle,
  i,
  NULL,  
 0
);

bResult=ReadFile(
		gd_handle,
		(LPVOID)buffer,4096,
		&rt,
		NULL
		);


if(bResult==FALSE)
  printf("failed to seek to block_bitmap... Error %ld.\n", GetLastError ());
else
{
	k=0;
	for(j=0;j<4096;j++)  //j should have upper limit= no of blocks available in each block grp which can be calculated using superblock(here it is 32768/8=984)
	{
		setbit=1;
		while(setbit<=256)
	 {
		
		printf("\n checked block no %d\n",k);
		
		if(buffer[j]&setbit)
		{
			printf("%d block is used\n", k);
		}
		
		else
		{
			//reserve the unused block..
			printf("\nbuffer[%d] = %d  before",j,buffer[j]);
			buffer[j]=buffer[j]|setbit;
			printf("\nbuffer[%d] = %d  after",j,buffer[j]);
			
			i.LowPart=gd_ptr->bg_block_bitmap*4096;
bResult=SetFilePointerEx(
  gd_handle,
  i,
  NULL,  
 0
);
			
			bResult=WriteFile(			gd_handle,
										buffer, 4096,
										&rt, NULL
							 );

			if(bResult) printf("block no %d reserved for inode no %d",k,inode_no);
			return k;			//block is free and return block no..
		}
		
		setbit=setbit<<1;
		k++;
	 }
	}
	
 }	

}







/***************************************************************************************

				
						GETORSETPTRVAL MODULE

		INPUT   : HANDLE TO THE PARTITION , BLOCKNO , BLOCKID , INODE NUMBER & FILEPATH				  
		OUTPUT  : POINTER TO BLOCK
		PROCESS : USING THE INPUT HANDLE GO TO THE INPUT BLOCKNO. THIS
				  BLOCK WILL CONTAIN A LIST OF BLOCK POINTERS.  ACCESS
				  THE POINTER INDEXED BY BLOCKID. RETRIEVE THE BLOCKNO
				  IT IS POINTING TO IF IT IS NOT 0 ELSE ALLOCATE A NEW
				  BLOCK AND UPDATE THE POINTER ACCORDINGLY. RETURN THE
				  POINTER TO THIS BLOCK.


**************************************************************************************/






__u32 GetOrSetPtrVal(
					HANDLE	gd_handle,
					__u32	blockno,
					__u32	blockid,
					int temp_inode,
					WCHAR	filePath[MAX_PATH]
				  )
{
	LARGE_INTEGER	i;
	char			buffer[4096];
	__u32			*blockno_ptr=malloc(sizeof(__u32));
	BOOL			bResult;
	DWORD			rt;
	
					i.LowPart=4096*blockno;
					i.HighPart=0;
					bResult=SetFilePointerEx(
												gd_handle,
												i,
												NULL, 
												0
											);

					bResult=ReadFile(
										gd_handle,
										(LPVOID)buffer,4096,
										&rt,
										NULL
									);

					memcpy(blockno_ptr,buffer+(blockid)*4,4);    
					if (*blockno_ptr==0)	//ALLOCATE BLOCK
					{
						*blockno_ptr = alloc_mem_single_block( temp_inode , filePath) ;	
						memcpy( buffer+(blockid)*4 , blockno_ptr , 4 );    
						bResult=SetFilePointerEx( gd_handle , i , NULL , 0 );
						WriteFile( gd_handle , buffer , 4096 , &rt , NULL );
					}
					return *blockno_ptr;
					
}





/***************************************************************************************

					WRITEFILE MODULE

		INPUT   : FILE NAME, BUFFER, BUFFERLENGTH, INPUT_OFFSET, DOKANFILEINFO
		OUTPUT  : SUCCESS/FAILURE AND NOMBER OF BYTES WRITTEN TO THE FILE
		PROCESS : LOCATE THE INODE CORRESPONDING TO INPUT FILENAME.CHECK WHETHER
				  SUFFICIENT MEMORY BLOCKS ARE ALLOCATED TO THE INODE.IF NOT THEN 
				  CALL 'MEMORY ALLOCATION' MODULE AND THEN RESERVE THE REQUIRED
				  NUMBER OF BLOCKS. WRITE THE CONTENTS OF THE BUFFER TO THE BLOCKS
				  SEQUENTIALLY AND CHANGE THE FILE SIZE ACCORDINGLY. RETURN STATUS 
				  AND NUMBER OF BYTES WRITTEN.

**************************************************************************************/





static int
WinFlux_WriteFile(
	LPCWSTR				FileName,
	LPCVOID				Buffer,
	DWORD				BufferLength,
	LPDWORD				NumberOfBytesWritten,
	LONGLONG			Input_Offset,
	PDOKAN_FILE_INFO	DokanFileInfo)
{

	int Status;
	WCHAR	filePath[MAX_PATH];
	HANDLE	handle = (HANDLE)DokanFileInfo->Context,gd_handle;
	BOOL	opened = FALSE,bResult;
	struct ext2_inode *inode_ptr;
	struct ext2_group_desc *gd_ptr;

	char copy_buffer[4096],buffer[4096];
	DWORD	rt;
	LARGE_INTEGER Offset;
	

	LARGE_INTEGER	LargeSize,i;
	ULONG			Remaining;	
	int				temp_inode,temp_block_group,temp_inode_index,current_block_id,WrittenSoFar;
	__u32			ReadSoFar = 0;	
	__u32			blockid,IndMax,DIndMax,TIndMax;
	__u32			*blockno_ptr;
	__u32			blockoffset;
	__u32			len;
	__u32			blockno;
	__u32			indblockno;
	__u32			dindblockno;
	__u32			BLIndMax;  // max block number in the indirect block
	__u32			BLDIndMax; // max block number in the double indirect block
	__u32			BLTIndMax; // max block number in the tripple indirect block
	__u32			PointersPerBlock;
	
    
	Offset.QuadPart=Input_Offset;
	
	
	Status =STATUS_SUCCESS;
	inode_ptr=malloc(256);
	gd_ptr=malloc(sizeof(struct ext2_group_desc)); 
	blockno_ptr=malloc(4);

	temp_inode=Check_In_Queue(FileName);

	GetFilePath(filePath, FileName);

    gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ|GENERIC_WRITE,     // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes

  if (gd_handle == INVALID_HANDLE_VALUE) // cannot open the drive
     return (FALSE);
  
	//now find appropiate block group..
	temp_block_group=(temp_inode-1) / 7872;
		
	//now find appropiate index..
	temp_inode_index=(temp_inode-1) % 7872;
	
	//set offset..
	i.LowPart=4096;
	i.HighPart=0;
	bResult=SetFilePointerEx(
								gd_handle,
								i,
								NULL, 
								0
							);

	bResult=ReadFile(
			gd_handle,
			(LPVOID)buffer,4096,
			&rt,
			NULL
			);

	// read first entry frm group_desc_table
	memcpy(gd_ptr,buffer+temp_block_group*32,32);    
	i.LowPart=gd_ptr->bg_inode_table*4096;

	//for reading inode table------------------------------
	bResult=SetFilePointerEx(
	gd_handle,
	i,
	NULL, 
	0
	);

	bResult=ReadFile(
		gd_handle,
		(LPVOID)buffer,4096,
		&rt,
		NULL
		);

	memcpy(inode_ptr,buffer+256*temp_inode_index,256);    
	
	//CODE FOR RETRIEVING INODE CONTENTS ENDS HERE..	
	
	
	
	inode_ptr->i_size=BufferLength;

	//CODE FOR WRITING BUFFER CONTENTS TO INODE BLOCKS STARTS HERE..

	PointersPerBlock = 4096 / 4;
	IndMax = PointersPerBlock + EXT2_NDIR_BLOCKS;
	DIndMax = PointersPerBlock * PointersPerBlock +  IndMax;
	TIndMax = PointersPerBlock * PointersPerBlock * PointersPerBlock +  DIndMax;

	Remaining = BufferLength;
	WrittenSoFar=0;

	while(Remaining > 0)
		{
		
			blockid = XtDiv64(WrittenSoFar, 4096, &blockoffset);


			if(blockid < EXT2_IND_BLOCK)
			{
				if( inode_ptr->i_block[blockid]==0 )		//ALLOCATE BLOCK
				{
					inode_ptr->i_block[blockid] = alloc_mem_single_block( temp_inode, filePath) ;
					printf("\n inode %d  has been allocated new memory block %d \n",temp_inode, blockno);
				}

				else 
					printf("\n inode %d already allocated block no %d \n",temp_inode, inode_ptr->i_block[blockid]);
				
				blockno = inode_ptr->i_block[blockid];
			}
			else if(blockid <  IndMax)
			{
				if( inode_ptr->i_block[EXT2_IND_BLOCK]==0 )		//ALLOCATE BLOCK
				{
					inode_ptr->i_block[EXT2_IND_BLOCK] = alloc_mem_single_block( temp_inode, filePath) ;
					printf("\n inode %d  has been allocated new memory block %d \n",temp_inode, blockno);
				}

				else 
					printf("\n inode %d already allocated block no %d \n",temp_inode, inode_ptr->i_block[EXT2_IND_BLOCK]);
				

				blockno = inode_ptr->i_block[EXT2_IND_BLOCK]; //GET BLOCKNO WHICH CONTAINS 1ST LEVEL INDIRECT PTRS
				if(blockno != 0)
				{
					blockid -= EXT2_IND_BLOCK;
					blockno=GetOrSetPtrVal(gd_handle,blockno,blockid,temp_inode,filePath);
							//BLOCKNO IS THE BLOCK WHICH CONTAINS THE 1ST LEVEL INDIRECT PTRS..THESE PTRS ARE INDEXED USING BLOCKID
				}
			}
			else if(blockid <  DIndMax)
			{
				if( inode_ptr->i_block[EXT2_DIND_BLOCK]==0 )		//ALLOCATE BLOCK
				{
					inode_ptr->i_block[EXT2_DIND_BLOCK] = alloc_mem_single_block( temp_inode, filePath) ;
					printf("\n inode %d  has been allocated new memory block %d \n",temp_inode, blockno);
				}

				else 
					printf("\n inode %d already allocated block no %d \n",temp_inode, inode_ptr->i_block[EXT2_IND_BLOCK]);
								
				blockno = inode_ptr->i_block[EXT2_DIND_BLOCK];
				if(blockno != 0)
				{
					blockid -=  IndMax;
					blockid = XtDiv1(blockid, 4096 / 4, &indblockno);
					blockno = GetOrSetPtrVal(gd_handle,blockno,blockid,temp_inode,filePath);	
							  // BLOCKNO IS THE BLOCK WHICH CONTAINS 1ST INDIRECT PTRS..BLOCKID IS THE INDEX INTO THIS BLOCK..
						      //RESULTANT BLOCKNO IS THE BLOCK WHICH WILL CONTAIN 2ND INDDIREC PTRS..INDBLOCKNO IS THE INDEX INTO THIS BLOCK						
					if(blockno != 0)
					{
						blockno = GetOrSetPtrVal(gd_handle,blockno,indblockno,temp_inode,filePath);	
					}
				}
			}
			else if(blockid <  TIndMax)
			{
				if( inode_ptr->i_block[EXT2_TIND_BLOCK]==0 )		//ALLOCATE BLOCK
				{
					inode_ptr->i_block[EXT2_TIND_BLOCK] = alloc_mem_single_block( temp_inode, filePath) ;
					printf("\n inode %d  has been allocated new memory block %d \n",temp_inode, blockno);
				}

				else 
					printf("\n inode %d already allocated block no %d \n",temp_inode, inode_ptr->i_block[EXT2_IND_BLOCK]);
								
				blockno = inode_ptr->i_block[EXT2_TIND_BLOCK];
				if(blockno != 0)
				{
					blockid -=  DIndMax;
					blockid = XtDiv1(blockid, 4096 / 4, &indblockno);
					blockid = XtDiv1(blockid, 4096 / 4, &dindblockno);
					blockno = GetOrSetPtrVal(gd_handle,blockno,blockid,temp_inode,filePath);	
					
					if(blockno != 0)
					{
					
						blockno = GetOrSetPtrVal(gd_handle,blockno,dindblockno,temp_inode,filePath);	
						if(blockno != 0)
						{
							blockno = GetOrSetPtrVal(gd_handle,blockno,indblockno,temp_inode,filePath);	
						}
					}
				}
			}
			else
			{
				Status = STATUS_END_OF_FILE;
			}

			if(Status == STATUS_SUCCESS)
			{
				if(Remaining > 4096) 
				{
					len = 4096;
				}
				else
				{
					len = Remaining;
				}

				if(len + blockoffset > 4096)
				{
					
					len = 4096 - blockoffset;
				}

				
				
				if(blockno == 0)
				{
					RtlFillMemory(Buffer, len, 0);
				}
				else
				{
					// copy the data
					i.LowPart=blockno*4096;
					i.HighPart=0;
					bResult=SetFilePointerEx(
												gd_handle,
												i,
												NULL,
												0
											);
					
					bResult=WriteFile(
										gd_handle,
										Buffer, 4096,//NumberOfBytesToWrite,
										NumberOfBytesWritten, NULL
									);

				}

				WrittenSoFar += len;
				Remaining -= len;
				(PCHAR)Buffer += len;
				blockid++;
			}
			else
			{
				break;
			}


		}
	
	
	//CODE FOR WITING BUFFER CONTENTS TO INODE BLOCKS ENDS HERE..

	//CODE FOR REGISTERING CHANGES TO INODE STRUCTURE  STARTS HERE..	
	
	memcpy(buffer+256*temp_inode_index,inode_ptr,256);
	
	i.LowPart=gd_ptr->bg_inode_table*4096;

	bResult=SetFilePointerEx(
	gd_handle,
	i,
	NULL, 
	0
	);

	bResult=WriteFile(gd_handle,buffer,4096,&rt, NULL);
	if(bResult==FALSE)printf("\nfailed to change inode %d contents... Error %ld.\n",temp_inode, GetLastError ());
	
	//CODE FOR REGISTERING CHANGES TO INODE STRUCTURE  ENDS HERE..
	return 0;
}





/***************************************************************************************


					WINFLUX_GETFILEINFORMATION MODULE

		INPUT   : FILE NAME AND HANDLE_FILE_INFORMATION
		OUTPUT  : THE CORRESPONDING INFORMATION REGARDING THE FILE 
				  IS SET PROPERLY IN THE HANDLE.
		PROCESS : THE FILE ATTRIBUTES THAT ARE IMPORTANT ARE SET
				  HERE WITH THE PROPER CORRESPONDING VALUES.

**************************************************************************************/





static int
WinFlux_GetFileInformation(
	LPCWSTR							FileName,
	LPBY_HANDLE_FILE_INFORMATION	HandleFileInformation,
	PDOKAN_FILE_INFO				DokanFileInfo)
{
	WCHAR	filePath[MAX_PATH];
	HANDLE	handle = (HANDLE)DokanFileInfo->Context;
	BOOL	opened = FALSE;
	struct ext2_inode *inode_ptr;
	int i,len,temp_inode=0;

	inode_ptr=malloc(256);

	GetFilePath(filePath, FileName);

	len=wcslen(FileName);
	
	for(i=0;i<len;i++)
	{
		if(FileName[i]=='.')
		{
			break;
		}
	}
	if(i==len)
	{
		DokanFileInfo->IsDirectory = TRUE;
		HandleFileInformation->dwFileAttributes=16;
	}
	else
	{
		DokanFileInfo->IsDirectory = FALSE;
		HandleFileInformation->dwFileAttributes=32;
		HandleFileInformation->nFileSizeHigh = 0;
		HandleFileInformation->nFileSizeLow = 0;
		temp_inode=Check_In_Queue(FileName);
		if(temp_inode!=-1)
		{
			Find_Inode_Info(temp_inode, &inode_ptr);
			HandleFileInformation->nFileSizeLow = inode_ptr->i_size;
		}
	
	}
	
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		handle = CreateFile(filePath, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (handle == INVALID_HANDLE_VALUE)
			return -1;
		opened = TRUE;
	}

	if (opened)
		CloseHandle(handle);

	return 0;
}





/***************************************************************************************


					WINFLUX_FINDFILES MODULE

		INPUT   : FILE NAME , FILLFINDDATA , DOKANFILEINFO
		OUTPUT  : SUCCESS / FAILURE
		PROCESS : READ DIRECTORY LISTING OF THE INPUT FILENAME AND RETURN THE
				  RESULT IN DOKANFILEINFO.


**************************************************************************************/




static int
WinFlux_FindFiles(
	LPCWSTR				FileName,
	PFillFindData		FillFindData, // function pointer
	PDOKAN_FILE_INFO	DokanFileInfo)
{

	int temp_inode=0,j,k=0,temp_loop_var;
	wchar_t local_path[EXT2_NAME_LEN];
  

  for(j=0;j<EXT2_NAME_LEN;j++)				//initialize temporary string for checking end-slash to NULL
	  local_path[j]='\0';

  wcsncpy(local_path,FileName,wcslen(FileName));


  if (wcslen(FileName) > 2)		//path is not root i.e. "\"
  {
	  int temp_len=wcslen(FileName);
	  char ch='\0';
	  ch=local_path[temp_len-1];
	  if(ch=='\\')					//check if last character is a slash '\'
	  {
									//if slash is found, return
		  return 0;
	  }
  }


  //Actual calls for directory listing function

  if(wcslen(FileName)==1)		//if root is not yet opened
  {
	  root=0;
	  Read_Directory_Listing(0,FileName,FillFindData,DokanFileInfo);
  }
  else							//if directory under root is accessed
  {
	  temp_inode=Check_In_Queue(FileName);

	  if(temp_inode==-1)		//if inode of the path is not found...
	  {
		  char temp_dir_name[EXT2_NAME_LEN];
		  struct node *head=NULL;

		  do					//traverse the path from end to separate the directories not yet listed and create a LL of these nodes.
		  {
			  k=0;
			  	for(j= (wcslen(local_path))-1 ;local_path[j]!='\\' ;j--);			//copy the name of last directory
				  
				for(temp_loop_var=j+1;local_path[temp_loop_var]!='\0';temp_loop_var++,k++)
				temp_dir_name[k]=local_path[temp_loop_var];

			  temp_dir_name[k]=local_path[j]='\0';


			  insert_into_node_list(&head,temp_dir_name);			//insert the directory name into node_list

			  temp_inode=Check_In_Queue(FileName);	//check for the new FileName if its cached
		  }while(temp_inode==-1);

		  Read_Directory_Listing(temp_inode,FileName,FillFindData,DokanFileInfo);		//send current path for directory listing

		  while(head!=NULL)				//traverse the LL to append and start directory listing
		  {
			  struct node *p;
			  p=head;
			  wcsncat(FileName,TEXT("\\"),1);		//create new FilePath
			  wcsncat(FileName,p->dir_name,strlen(p->dir_name));

			  temp_inode=Check_In_Queue(FileName);	
			  Read_Directory_Listing(temp_inode,FileName,FillFindData,DokanFileInfo);
			  head=head->next;				//proceed to next sub-directory
			  free(p);						//free the traversed node
		  }

	  }

	  Read_Directory_Listing(temp_inode,FileName,FillFindData,DokanFileInfo);
  }
	return 0;
}






/***************************************************************************************


					WINFLUX_UNMOUNT MODULE

		RETURN SUCCESS i.e. 0.


**************************************************************************************/



static int
WinFlux_Unmount(
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	return 0;
}


/***************************************************************************************


					WINFLUX_READFILE MODULE

		INPUT   : FILE NAME
		OUTPUT  : THE CORRESPONDING INFORMATION REGARDING THE FILE 
				  IS FETCHED AND DISPLAYED.
		PROCESS : THE INODE LOCATION OF THE FILE NAME IS OBTAINED.
				  THEN i_block[] FIELS OF THE INODE IS TRAVERSED
				  TILL THE END OF THE FILE SIZE AND ALL THE DATA 
				  IS READ INTO A BUFFER WHICH IS RETURNED BACK.


**************************************************************************************/



static int
WinFlux_ReadFile(
	LPCWSTR				FileName,
	LPVOID				Buffer,
	DWORD				BufferLength,
	LPDWORD				ReadLength,
	LONGLONG			Input_Offset,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	
	int Status;
	WCHAR	filePath[MAX_PATH];
	HANDLE	handle = (HANDLE)DokanFileInfo->Context,gd_handle;
	BOOL	opened = FALSE,bResult;
	struct ext2_inode *inode_ptr;
	struct ext2_group_desc *gd_ptr;

	char copy_buffer[4096],buffer[4096];
	DWORD	rt;
	LARGE_INTEGER Offset;
	

	LARGE_INTEGER	LargeSize,i;
	ULONG			Remaining;	
	int				temp_inode,temp_block_group,temp_inode_index;
	__u32			ReadSoFar = 0;	
	__u32			blockid,IndMax,DIndMax,TIndMax;
	__u32			*blockno_ptr;
	__u32			blockoffset;
	__u32			len;
	__u32			blockno;
	__u32			indblockno;
	__u32			dindblockno;
	__u32			BLIndMax;  // max block number in the indirect block
	__u32			BLDIndMax; // max block number in the double indirect block
	__u32			BLTIndMax; // max block number in the tripple indirect block
	__u32			PointersPerBlock;
	
    
	Offset.QuadPart=Input_Offset;
	
	
	Status =STATUS_SUCCESS;
	inode_ptr=malloc(256);
	gd_ptr=malloc(sizeof(struct ext2_group_desc)); 
	blockno_ptr=malloc(4);

	temp_inode=Check_In_Queue(FileName);					// OBTAIN THE INODE NO OF THE FILE

	GetFilePath(filePath, FileName);

    gd_handle = CreateFile(filePath,  // drive to open   
                    GENERIC_READ,     // no access to the drive
                    FILE_SHARE_READ | // share mode
                    FILE_SHARE_WRITE, 
                    NULL,             // default security attributes
                    OPEN_EXISTING,    // disposition
                    0,                // file attributes
                    NULL);            // do not copy file attributes

  if (gd_handle == INVALID_HANDLE_VALUE) // cannot open the drive
     return (FALSE);
  
	//now find appropiate block group..
	temp_block_group=(temp_inode-1) / 7872;
		
	//now find appropiate index..
	temp_inode_index=(temp_inode-1) % 7872;
	
	//set offset..
	i.LowPart=4096;
	i.HighPart=0;
	bResult=SetFilePointerEx(
								gd_handle,
								i,
								NULL, 
								0
							);

	bResult=ReadFile(
			gd_handle,
			(LPVOID)buffer,4096,
			&rt,
			NULL
			);

	// read corrosponding entry frm group_desc_table
	memcpy(gd_ptr,buffer+temp_block_group*32,32);    
	i.LowPart=gd_ptr->bg_inode_table*4096;

	//for reading inode table------------------------------
	bResult=SetFilePointerEx(
	gd_handle,
	i,
	NULL, 
	0
	);

	bResult=ReadFile(
		gd_handle,
		(LPVOID)buffer,4096,
		&rt,
		NULL
		);

	memcpy(inode_ptr,buffer+256*temp_inode_index,256);    
	LargeSize.LowPart=inode_ptr->i_size;
	LargeSize.HighPart=0;
	
	PointersPerBlock = 4096 / 4;
	IndMax = PointersPerBlock + EXT2_NDIR_BLOCKS;
	DIndMax = PointersPerBlock * PointersPerBlock +  IndMax;
	TIndMax = PointersPerBlock * PointersPerBlock * PointersPerBlock +  DIndMax;

	Remaining = BufferLength;			// ACTUAL FILE SIZE IS SET HERE
	
	while(Remaining > 0)
		{
		
			blockid = XtDiv64(Offset.QuadPart + ReadSoFar, 4096, &blockoffset);
			if(blockid < EXT2_IND_BLOCK)		// IF DIRECT BOLCK
			{
				blockno = inode_ptr->i_block[blockid];
			}
			else if(blockid <  IndMax)			// IF SINGLY INDIRECT BOLCK
			{
				blockno = inode_ptr->i_block[EXT2_IND_BLOCK];
				if(blockno != 0)
				{
					blockid -= EXT2_IND_BLOCK;
					blockno = FetchPtrVal(gd_handle,blockno,blockid);
				}
			}
			else if(blockid <  DIndMax)			// IF DOUBLY INDIRECT BOLCK
			{
				blockno = inode_ptr->i_block[EXT2_DIND_BLOCK];
				if(blockno != 0)
				{
					blockid -=  IndMax;
					blockid = XtDiv1(blockid, 4096 / 4, &indblockno);
					blockno = FetchPtrVal(gd_handle,blockno,blockid);
					if(blockno != 0)
					{
						blockno = FetchPtrVal(gd_handle,blockno,indblockno);
					}
				}
			}
			else if(blockid <  TIndMax)			// IF TRIPLY INDIRECT BOLCK
			{
				blockno = inode_ptr->i_block[EXT2_TIND_BLOCK];
				if(blockno != 0)
				{
					blockid -=  DIndMax;
					blockid = XtDiv1(blockid, 4096 / 4, &indblockno);
					blockid = XtDiv1(blockid, 4096 / 4, &dindblockno);
					blockno = FetchPtrVal(gd_handle,blockno,blockid);
					if(blockno != 0)
					{
						blockno = FetchPtrVal(gd_handle,blockno,dindblockno);
						if(blockno != 0)
						{
							blockno = FetchPtrVal(gd_handle,blockno,indblockno);
						}
					}
				}
			}
			else
			{
				Status = STATUS_END_OF_FILE;
			}

			if(Status == STATUS_SUCCESS)
			{
				if(Remaining > 4096) // we only read one block at a time
				{
					len = 4096;
				}
				else
				{
					len = Remaining;
				}

				if(len + blockoffset > 4096)
				{
					// reduce the length because we are not reading the entire block
					len = 4096 - blockoffset;
				}

				
				// blockno now has the next block that we need to read...
				if(blockno == 0)
				{
					// this is a sparse file
					RtlFillMemory(Buffer, len, 0);
				}
				else
				{
					// copy the data
					i.LowPart=blockno*4096;
					i.HighPart=0;
					bResult=SetFilePointerEx(
												gd_handle,
												i,
												NULL,
												0
											);
					
					bResult=ReadFile(
										gd_handle,
										Buffer,len,
										&rt,
										NULL
									);

				}

				*ReadLength=ReadSoFar += len;
				Remaining -= len;
				(PCHAR)Buffer += len;
			}
			else
			{
				break;
			}


		}
		// update the file offset
		Offset.QuadPart = Offset.QuadPart + BufferLength;
		return 0;
}


/***************************************************************************************


					DOKAN_OPEREATIONS

		IMPORTANCE : HERE THE ACTUAL MAPPING OF THE CALLBACK FUNCTIONS IS 
					 IS DONE WITH THE DOKAN LIBRARY FUNCTIONS IN THE PROPER 
					 EXPECTED SEQUENCE OF FUNCTIONS.


**************************************************************************************/




static DOKAN_OPERATIONS
dokanOperations = {
	WinFlux_CreateFile,
	WinFlux_OpenDirectory,
	NULL,//WinFlux_CreateDirectory,
	WinFlux_Cleanup,
	WinFlux_CloseFile,
	WinFlux_ReadFile,
	WinFlux_WriteFile,
	NULL,//WinFlux_FlushFileBuffers,
	WinFlux_GetFileInformation,
	WinFlux_FindFiles,
	NULL, // FindFilesWithPattern
	NULL, //WinFlux_SetFileAttributes,
	NULL,//WinFlux_SetFileTime,
	NULL,//WinFlux_DeleteFile,
	NULL,//WinFlux_DeleteDirectory,
	NULL,//WinFlux_MoveFile,
	NULL,//WinFlux_SetEndOfFile,
	NULL,//WinFlux_LockFile,
	NULL,//WinFlux_UnlockFile,
	WinFlux_GetDiskFreeSpace,
	WinFlux_GetVolumeInformation,
	WinFlux_Unmount 
};


/***************************************************************************************


					MAIN

		INPUT   : ARGC AND ARGV
		OUTPUT  : SUCCESS/FAILURE
		PROCESS : CHECKS IF THE MOUNTED PARTITION IS EXT2 OR NOT
				  IF SO THEN CONTINUES NORMAL FUNCTION CALLS ELSE
				  RETURNS AN ERROR


**************************************************************************************/



int __cdecl
main(ULONG argc, PCHAR argv[])
{
	int status;
	
	PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));

	if (argc < 3) {

		return -1;
	}

	ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));

	mbstowcs(RootDirectory, argv[1], strlen(argv[1]));

	dokanOptions->DriveLetter = argv[2][0];

	if (argc == 4)
		dokanOptions->ThreadCount = atoi(argv[3]);
	else
		dokanOptions->ThreadCount = 1;
	
	dokanOptions->DebugMode = 1;
	dokanOptions->UseKeepAlive = 1;

	Read_Superblock();
	if(sb_ptr->s_magic!=61267)
	{
		fprintf(stderr,"\nPartition is not Ext2 formatted..");	
		return 0;
	}
	status = DokanMain(dokanOptions, &dokanOperations);
	switch (status) {
		case DOKAN_SUCCESS:
			fprintf(stderr, "Success\n");
			break;
		case DOKAN_ERROR:
			fprintf(stderr, "Error\n");
			break;
		case DOKAN_DRIVE_LETTER_ERROR:
			fprintf(stderr, "Bad Drive letter\n");
			break;
		case DOKAN_DRIVER_INSTALL_ERROR:
			fprintf(stderr, "Can't install driver\n");
			break;
		case DOKAN_START_ERROR:
			fprintf(stderr, "Driver something wrong\n");
			break;
		case DOKAN_MOUNT_ERROR:
			fprintf(stderr, "Can't assign a drive letter\n");
			break;
		default:
			fprintf(stderr, "Unknown error: %d\n", status);
			break;
	}
	return 0;
}
