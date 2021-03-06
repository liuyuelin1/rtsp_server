#include "rtp.h"
#include "../rtcp/rtcp.h"
#include "../rtsp/rtsp.h"
#include "../comm/type.h"


UL64 get_randdom_seq(VOID)
{
    UL64 seed;
    srand((unsigned)time(NULL));  
    seed = 1 + (U32) (rand()%(0xFFFF)); 
    
    return seed;
}




/**********************************************************************
 *
 *  获取文件视频大小
 *
 *
 *
 *
 * ******************************************************************/
L64 get_file_size(FILE *infile)
{
	L64 size_of_file;
	
	/* jump to the end of the file */
	fseek(infile, 0L, SEEK_END);
	/* get the end position */
	size_of_file = ftell(infile);
	
	/* jump back to the original position */
	fseek(infile, 0L, SEEK_SET);
		
	return (size_of_file);
}
	


S32 wirte_to_file(U8 *buf, S32 buffer_size, FILE *outfile)
{
	L64 nbytes_read;
	
	nbytes_read = fwrite(buf, 1, buffer_size, outfile);
	
	return nbytes_read;
}




S32 my_strlen(const CHAR *str)
{
	S32 ret = 0;
	const CHAR* p = str;
	
	if(NULL == str)
		return -1;
	
	while(*p++)
	{
		ret++;
	}
	
	return ret;
}


UL64 get_random_seq(VOID)
{
	UL64 seed;
	srand((unsigned)time(NULL));
	seed = 1 + (U32)(rand()%(0XFFFF));

	return seed;
}


UL64 get_random_timestamp(VOID)
{
	UL64 seed;
	srand((unsigned) time(NULL));
	seed = 1 + (U32) (rand()%(0xFFFFFFFF));

	return seed;
}


L64 get_timestamp()
{
	struct timeval tv_date;

	gettimeofday(&tv_date, NULL);
	
	return ((L64)tv_date.tv_sec * 1000000 + (L64)tv_date.tv_usec);
}

S32 build_rtp_header(RTP_header *r, S32 cur_conn_num)
{
	r->version = 2;
	r->padding = 0;
	r->extension = 0;
	r->csrc_len = 0;
	r->marker = 0;
	r->payload = 96;
	r->seq_no = htons(rtsp[cur_conn_num]->cmd_port.seq);
	rtsp[cur_conn_num]->cmd_port.timestamp += rtsp[cur_conn_num]->cmd_port.frame_rate_step;
	r->timestamp = htonl(rtsp[cur_conn_num]->cmd_port.timestamp);
	r->ssrc = htonl(rtsp[cur_conn_num]->cmd_port.ssrc);

	return 0;
}



ssize_t write_n(S32 fd, const VOID *vptr, size_t n )
{
	size_t nleft;
	ssize_t nwritten;
	const CHAR *ptr;
	
	ptr = vptr;
	nleft = n;
	
	while(nleft >0)
	{
		if((nwritten = write(fd, ptr, nleft)) <= 0)
		{
			if(errno == EINTR)
				nwritten  =0; 		//call write () again
			else
				return -1;			//error
		}
		
		nleft -= nwritten;
		ptr += nwritten;
	}

	return n;

}

/*********************************************************
 *
 *
 *			RTP_header: rtp header struct
 *			len: upd bufer len
 *			cur_conn_num :current connect number
 *
 *
 * *******************************************************/
S32 udp_write(S32 len, S32 cur_conn_num)
{
	S32 result;
	
	result = write(rtsp[cur_conn_num]->fd.video_rtp_fd, rtsp[cur_conn_num]->nalu_buffer, len);
	if(result <= 0)
		rtsp[cur_conn_num]->rtspd_status = 0x21;

	return 0;
}


S32 udp_write_fua(S32 len, S32 time, S32 cur_conn_num)
{
	S32 result;
	again:
	result = write(rtsp[cur_conn_num]->fd.video_rtp_fd, rtsp[cur_conn_num]->nalu_buffer, len);
	if(result <= 0)
		goto again;
	else
	{
		if(time > DE_TIME)
			time = DE_TIME;

		usleep(time);
	}	
	return 0;	
}


S32 abstr_nalu_indic(U8 *buf, S32 buf_size, S32 *be_found)
{
	U8 *p_tmp;
	S32 offset;
	S32 frame_size;
	
	*be_found = 0;
	offset = 0;
	frame_size = 4;
	p_tmp = buf + 4;
	
	while(frame_size < buf_size - 4)
	{
		if(p_tmp[2])
			offset = 4;
		else if(p_tmp[1])
			offset = 2;
		else if(p_tmp[0])
			offset = 1;
		else
		{
			if(p_tmp[3] != 1)
			{
				if(p_tmp[3])
					offset = 4;
				else
					offset = 1;
			}
			else
			{
				*be_found = 1;
				break;
			}

		}
			
		frame_size += offset;
		p_tmp += offset;
	}

	if(!*be_found)
		frame_size = buf_size;

	return frame_size;
}

/********************************************************************************
 * RTP Packet:
 * 1. NALU length small than 1460-sizeof(RTP header):
 *	(RTP Header) + (NALU without Start Code)
 * 2. NALU length larger the MTU
 *  (RTP Header) + (FU Indicator) + (FU Header) + (NALU Slice)
 *  			 + (FU Indicator) + (FU Header) + (NALU Slice)
 *				 + ....
 *
 *	inbuffer -- NALU: 00 00 00 01    1 Byte       XX XX XX
 *				    |Start Code |  |NALU Header| |NALU Data|
 *				    
 *  NALU Slice : Cut NALU Data into Slice.
 *  
 *	NALU Header : F|NRI|TYPE
 *				  F: 1 bit
 *				  NRI: 2 bit
 *				  Type: 5 bit
 *
 *	FU Indicator : Like NALU Header, Type in FU-A(28)
 *
 *  FU Header : S|E|R|Type
 *  			S : 1bit , Start, First Slice should set
 *  			E : 1bit , End, Last Slice should set
 *  			R : 1bit , Reserved
 *  			Type: 5bit, Same with NALU Header's Type
 * ***********************************************************************/
S32 build_rtp_nalu(U8 *inbuffer, S32 frame_size, S32 cur_conn_num)
{
	RTP_header rtp_header;
	S32 time_delay;
	S32 data_left;
	
	U8 nalu_header;
	U8 fu_indic;
	U8 fu_header;
	U8 *p_nalu_data;
	U8 *nalu_buffer;
	
	S32 fu_start = 1;
	S32 fu_end = 0;
	
	if(!inbuffer)
		return -1;

	nalu_buffer = rtsp[cur_conn_num]->nalu_buffer;
	
	build_rtp_header(&rtp_header, cur_conn_num);
	
	data_left = frame_size - NALU_INDIC_SIZE;
	p_nalu_data = inbuffer + NALU_INDIC_SIZE;
	
	//Single RTP Packet
	if(data_left <= SINGLE_NALU_DATA_MAX)
	{
		rtp_header.seq_no = htons(rtsp[cur_conn_num]->cmd_port.seq++);
		rtp_header.marker = 1;
		memcpy(nalu_buffer, &rtp_header, sizeof(rtp_header));
		memcpy(nalu_buffer + RTP_HEADER_SIZE, p_nalu_data, data_left);
		udp_write(data_left + RTP_HEADER_SIZE, cur_conn_num);
		usleep(DE_TIME);
		
		return 0;
	}

	//FU-A RTP Packet.
	nalu_header = inbuffer[4];
	fu_indic = (nalu_header & 0xE0)|28;
	data_left   -= NALU_HEAD_SIZE;
	p_nalu_data += NALU_HEAD_SIZE;

	while(data_left > 0)
	{
		S32 proc_size = MIN(data_left, SLICE_NALU_DATA_MAX);
		S32 rtp_size = proc_size 	+
					RTP_HEADER_SIZE +	
					FU_A_HEAD_SIZE  +
					FU_A_INDI_SIZE ;

		fu_end = (proc_size == data_left);
		fu_header = nalu_header&0x1F;
		
		if(fu_start)
			fu_header |= 0x80;
		else if(fu_end)
			fu_header |= 0x40;
			
			
		rtp_header.seq_no = htons(rtsp[cur_conn_num]->cmd_port.seq ++);
		memcpy(nalu_buffer, &rtp_header, sizeof(rtp_header));
		memcpy(nalu_buffer + 14, p_nalu_data, proc_size);
		nalu_buffer[12] = fu_indic;
		nalu_buffer[13] = fu_header;
		udp_write(rtp_size, cur_conn_num);
		if(fu_end)
			usleep(36000);
	
		data_left -= proc_size;	
		p_nalu_data += proc_size;
		fu_start = 0;
	}

	return 0;
}



S32 rtp_send_from_file(S32 cur_conn_num)
{
	FILE *infile = NULL, *outfile = NULL;
	S32 total_size = 0, bytes_consumed = 0, frame_size = 0, bytes_left;
	U8 inbufs[READ_LEN] = "", outbufs[READ_LEN] = "";
	U8 *p_tmp = NULL;

	S32 found_nalu = 0;
	S32 reach_last_nalu = 0;
		
	//infile = fopen(rtsp[0]->file_name, "rb");
	infile = fopen("./1.h264", "rb");
	
	if(infile == NULL)
	{
		printf("please check media file\n");
	}

	total_size = get_file_size(infile);
	if(total_size <= 4)
	{
		fclose(infile);
		return 0;
	}	

	#ifdef WRITE_FILE
	outfile = fopen("1.h264", "w");
	if(outfile == NULL)
	{
		printf("please check media file\n");
		return -1;
	}
	#endif	

	rtsp[cur_conn_num]->is_runing = 1;
	while(rtsp[cur_conn_num]->is_runing)
	{
		bytes_left = fread(inbufs, 1, READ_LEN, infile);
		p_tmp = inbufs;
		
		while(bytes_left > 0)
		{

			frame_size = abstr_nalu_indic(p_tmp, bytes_left, &found_nalu);
			reach_last_nalu = (bytes_consumed + frame_size >= total_size);
	
			if(found_nalu || reach_last_nalu)
			{
				memcpy(outbufs, p_tmp, frame_size);
				
				#ifdef WRITE_FILE
					wirte_to_file(outbuf, frame_size, outfile);
				#endif 

				build_rtp_nalu(outbufs, frame_size, cur_conn_num);
				bytes_consumed += frame_size;
			
				if(reach_last_nalu)
					rtsp[cur_conn_num]->is_runing = 0;		
			}
			bytes_left -= frame_size;	

		}		
		fseek(infile, bytes_consumed, SEEK_SET);
	}

	fclose(infile);
	close(rtsp[cur_conn_num]->fd.video_rtp_fd);

	#ifdef WRITE_FILE
		fclose(outfile);	
	#endif
}


S32 rtp_send_from_stream()
{
	return 0 ;
}

S32 rtp_send_packet(S32 cur_conn_num)
{
	rtp_send_from_file(cur_conn_num);
	return 0;

}
