#include "modbus.h"

struct Callbacks_struct callbacks;

int deniedIP[MODBUS_MAX_DENIED_IP];

int recvall_ov(WSAOVERLAPPED* clientOverlapped,HANDLE ev,SOCKET s, char* buf,
				 int len,DWORD timeout,DWORD flags)
{
	DWORD res = 0, templen = len,tempflags = flags;
	int i = 0;
	WSABUF dataBuf;
	int waitRes;
	
	ZeroMemory(clientOverlapped,
		sizeof(WSAOVERLAPPED));

	clientOverlapped->hEvent = ev;
	WSAResetEvent(clientOverlapped->hEvent);
	
	
	do {
		dataBuf.len = templen;
		dataBuf.buf = &buf[i];
		flags = tempflags;

        if(WSARecv(s,&dataBuf,
				1,&res,&flags,clientOverlapped,NULL)==SOCKET_ERROR)
		{
			int x = WSAGetLastError();
			if(x != WSA_IO_PENDING)
			{
				return 0;
			}
		}
		waitRes = WSAWaitForMultipleEvents(1,&clientOverlapped->hEvent,false,
			timeout,FALSE);
		if(waitRes == WSA_WAIT_TIMEOUT)
		{
			return 0;
		}
		WSAResetEvent(clientOverlapped->hEvent);

		WSAGetOverlappedResult(s,clientOverlapped,
			&res,false,&flags);

        if ( res > 0 )
		{
			templen -= res;
			i += res;
		}
		else 
			return res;
    } while( templen > 0 );
	return len;
}

int sendall_ov(WSAOVERLAPPED* clientOverlapped,HANDLE ev, SOCKET s, char* buf,
				 int len,DWORD timeout,DWORD flags)
{
	DWORD res = 0, templen = len,tempflags = flags;
	int i = 0;
	WSABUF dataBuf;
	int waitRes;
	
	ZeroMemory(clientOverlapped,
		sizeof(WSAOVERLAPPED));

	clientOverlapped->hEvent = ev;
	WSAResetEvent(clientOverlapped->hEvent);
	
	
	do {
		dataBuf.len = templen;
		dataBuf.buf = &buf[i];
		flags = tempflags;

        if(WSASend(s,&dataBuf,
				1,&res,flags,clientOverlapped,NULL)==SOCKET_ERROR)
		{
			int x = WSAGetLastError();
			if(x != WSA_IO_PENDING)
			{
				return 0;
			}
		}
		waitRes = WSAWaitForMultipleEvents(1,&clientOverlapped->hEvent,false,
			timeout,FALSE);
		if(waitRes == WSA_WAIT_TIMEOUT)
		{
			return 0;
		}
		WSAResetEvent(clientOverlapped->hEvent);

		WSAGetOverlappedResult(s,clientOverlapped,
			&res,false,&flags);

        if ( res > 0 )
		{
			templen -= res;
			i += res;
		}
		else 
			return res;
    } while( templen > 0 );
	return len;
}



bool ModbusReadDeniedIP()
{
	FILE* f;
	char ipbuf[IPBUFSIZE];
	int i = 0;

	if(_wfopen_s(&f,L"denied.inf",L"r")!= 0)
		return false;
	
	while((fgets(ipbuf,IPBUFSIZE,f)!=NULL) && (i < MODBUS_MAX_DENIED_IP))
	{
		deniedIP[i] = inet_addr(ipbuf);
		i++;
	}
	fclose(f);
	return true;
}

int ModbusInit(u_int localaddr, u_int port)
{
	WORD wVersionRequested;
	WSADATA wsaData;
	SOCKET sockServer;
	struct sockaddr_in saServer;
	unsigned threadID;
	uintptr_t threadHandle;
	int err;

	if(!ModbusReadDeniedIP())
	{
		WriteLog(L"���������� ������� ���� denied.inf");
		return -1;
	}

	wVersionRequested = MAKEWORD( 2, 2 );

	err = WSAStartup(wVersionRequested,&wsaData);
	if (err != 0)
	{
		WriteLogWSAError(L"���������� ���������������� ���������� WinSock");
		return -1;
	}
	WriteLog(L"������������� ���������� WinSock ���������");
	
	saServer.sin_addr.s_addr = htonl(localaddr);
	saServer.sin_family = AF_INET;														
	saServer.sin_port = htons(port);

	sockServer = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if(sockServer == SOCKET_ERROR)
	{
		WriteLogWSAError(L"������ ������������� ������");
		return -1;
	}
	
	WriteLog(L"����� ������� ������, Socket ID = %d",sockServer);

	if(bind(sockServer,(struct sockaddr*)&saServer,
		sizeof(saServer)) == SOCKET_ERROR)
	{
		closesocket(sockServer);
		WriteLogWSAError(L"������ �������� ������");
		return -1;
	}
	WriteLog(L"����� ������� ��������");

	if(listen(sockServer,SOMAXCONN)==SOCKET_ERROR)
	{
		closesocket(sockServer);
		WriteLogWSAError(L"������ �������������");
		return -1;
	}
	WriteLog(L"������������� ����� ��������");

	//�.�. � C ������ ���������� ���������� �� ��������, �������� �����
	//������ ������� ��� "�����" � ��������� �����
	if(threadHandle = _beginthreadex( NULL, 0,&ServerRoutine,
			(void*)sockServer,0 ,&threadID)==-1L)
	{
		closesocket(sockServer);
		WriteLog(L"���������� ��������� ��������� �����");
		return -1;
	}
	return 0;
}

unsigned _stdcall ServerRoutine(void* socket)
{
	SOCKET clientSock;
	
	uintptr_t clientThread; 
	uintptr_t threadID;
	uintptr_t curThreadID = GetCurrentThreadId();
	bool res = false;
	int i = 0;
	int ret = 0;

	struct sockaddr localaddr;
	int size = sizeof(struct sockaddr);

	//����� �������, �������� �������� ��� �����
	SOCKET serverSock = (SOCKET) socket;

	WriteLog(L"����� ������� �������, ThreadID = %d",curThreadID);

	while(true)
	{
		if((clientSock = accept(serverSock,
			(struct sockaddr*)&localaddr,&size))
			== INVALID_SOCKET)
		{
			WriteLogWSAError(L"����� ��������������");
			ret = 1;
			break;
		}

		//�������� ���������� ip-������
		for (i = 0;i < MODBUS_MAX_DENIED_IP;i++)
		{
			if(((struct sockaddr_in*)&localaddr)->sin_addr.S_un.S_addr == deniedIP[i])
			{
				res = true;
				break;
			}
		}

		if(res)
		{
			WriteLog(L"IP-����� ������� ��������� � ������ �����������");
			closesocket(clientSock);
			continue;
		}

		WriteLog(L"���������� �����������, Socket ID = %d",clientSock);

		//�.�. � C ������ ���������� ���������� �� ��������, �������� �����
		//������ ������� ��� "�����" � ���������� �����
		if(clientThread = _beginthreadex( NULL, 0,&ClientRoutine,
		 	(void*)clientSock,0 ,&threadID)==-1L)
		{
			WriteLog(L"���������� ��������� ���������� ����� ��� ������ %d",
				clientSock);
			ret = 1;
			break;
		}
	}
	WriteLog(L"����� ������� N = %d ������",serverSock);

	closesocket(serverSock);

	WriteLog(L"���������� ���������� ������, ThreadID = %d",curThreadID);
	_endthreadex(ret);
	return ret;
}

unsigned _stdcall ClientRoutine(void* socket)
{
	SOCKET clientSock = (SOCKET) socket;

	int ret = 0;
	DWORD recvBytes = 0, flags = 0;
	HANDLE wsaEvent = WSACreateEvent();

	char mbap_buffer[MODBUS_MPAB_LEN];
	char frame_buffer[MODBUS_MAX_PDU];
	char receive_buffer[MODBUS_MAX_ADU];

	WSAOVERLAPPED clientOverlapped;
	const int MAXFRAME_LEN = 1024;
	uintptr_t curThreadID = GetCurrentThreadId();
	char num = 0;
	int datasize;

	WriteLog(L"����� ������� �������, ThreadID = %d",curThreadID);

	while(true)
	{

		//������ MBAP
		int waitRes = recvall_ov(&clientOverlapped,wsaEvent,
			clientSock,mbap_buffer,
			MODBUS_MPAB_LEN,MODBUS_TRANS_TIMEOUT,0);
		if(waitRes == 0)
		{
			WriteLog(L"������ ������ MBAP ��� ����� ����-��� �������� MBAP");
			ret = 1;
			break;
		}

		//����� ���������
		//---------------------------------------------------------------
		//0			1		2		3		4		5			6		
		// ID ���������� | ID-��������� | ����� ��������� | ID ����������
		//				 |				|				  | ������������
		//---------------|--------------|-----------------|--------------
		//����������	 |		0		|���������������� | ���������� 
		//�� ����������� |				|�������� �		  |�� �����������
		//�������		 |				|��������		  |�������
		//---------------------------------------------------------------

		//������ ������ ������, ���������� �� Big_Endian ������� �
		//Little_endian ��� ����������� Intel
		datasize = GetTCPShort(&mbap_buffer[4]) - 1;

		if(mbap_buffer[2] != 0 || mbap_buffer[3] != 0)
		{
			WriteLog(L"������������ MBAP");
			ret = 1;
			break;
		}
		if(datasize < 2)
		{
			WriteLog(L"������ ����� ������ �� ����� ���� <= 0");
			ret = 1;
			break;
		}
		if(datasize > MODBUS_MAX_PDU)
		{
			WriteLog(L"������ ����� ������ ������� ��������� ����������� ����������");
			ret = 1;
			break;
		}
	
		//������ ������
		waitRes = recvall_ov(&clientOverlapped,wsaEvent,
			clientSock,frame_buffer,
			datasize,MODBUS_TRANS_TIMEOUT,0);
		if(waitRes == 0)
		{
			WriteLog(L"������ ������ ������ ��� ����� ����-��� �������� ������");
			ret = 1;
			break;
		}

		switch (frame_buffer[0])
		{
		//������ ���������� ������
		case MODBUS_READ_DISCRETE_INPUTS:
			num = ModbusReadDiscreteInputsFunc(mbap_buffer,frame_buffer);
			break;
		//������ ������� ���������
		case MODBUS_READ_INPUT_REGISTERS:
			num = ModbusReadInputRegisters(mbap_buffer,frame_buffer);
			break;
		//������ �������� ���������
		case MODBUS_WRITE_SINGLE_COIL:
			num = ModbusWriteSingleCoilFunc(mbap_buffer,frame_buffer);
		//������ ����������������� ����������
		case MODBUS_READ_DEVICE_IDENTIFICATION:
			num = ModbusReadDeviceIdentificationFunc(mbap_buffer,frame_buffer);
		default:
			num = ModbusExceptionRsp(mbap_buffer,frame_buffer,MODBUS_ILLEGAL_FUNCTION);
		}

		memcpy_s(receive_buffer,MODBUS_MPAB_LEN,mbap_buffer,MODBUS_MPAB_LEN);
		memcpy_s(receive_buffer+MODBUS_MPAB_LEN,num,frame_buffer,num);

		waitRes = sendall_ov(&clientOverlapped,wsaEvent,
			clientSock,receive_buffer,
			MODBUS_MPAB_LEN+num,MODBUS_TRANS_TIMEOUT,0);
		if(waitRes == 0)
		{
			WriteLog(L"������ � ���������� �������� ��� ����� ����-��� ��������");
			ret = 1;
			break;
		}
		/*waitRes = sendall_ov(&clientOverlapped,wsaEvent,
			clientSock,mbap_buffer,
			MODBUS_MPAB_LEN,MODBUS_TRANS_TIMEOUT,0);
		if(waitRes == 0)
		{
			WriteLog(L"������ � ���������� �������� MBAP ��� ����� ����-��� ��������");
			ret = 1;
			break;
		}
		waitRes = sendall_ov(&clientOverlapped,wsaEvent,
			clientSock,frame_buffer,
			num,MODBUS_TRANS_TIMEOUT,0);
		if(waitRes == 0)
		{
			WriteLog(L"������ � ���������� �������� ������ ��� ����� ����-��� ��������");
			ret = 1;
			break;
		}*/
	}
	WriteLog(L"���������� � �������� ���������, SocketID = %d",clientSock);
	closesocket(clientSock);
	WSACloseEvent(wsaEvent);

	WriteLog(L"���������� ����������� ������, ThreadID = %d",curThreadID);
	_endthreadex(ret);	
	return ret;
}

/*������� modbus
	0x02 - ������ ������� ���������� ��������� 

	������ ���� ��������� (�����):
		0	-	0�01
		1,2		-	��������� �������
		3,4		-	���������� ������������� ���������
*/
char ModbusReadDiscreteInputsFunc(char* mbap, char* frame)
{
	//�����, ������������ � ������ MBAP = ����� ������ + ID slave
	char mbaplen;
	//����� ��������� ���������
	char regbuf[MODBUS_MAX_DISCRETE_INPUTS];
	//���������� ������ ��������� ��������� � �������� ���������
	char quantity_of_bytes;

	//��������� ��������� �������

	//��������� �����
	u_short startaddr = GetTCPShort(&frame[1]);
	//���-�� ������������� ���������
	u_short quantity = GetTCPShort(&frame[3]);

	//����������� �������� ��������� ������� �� ���������� ��������
	if(startaddr > MODBUS_MAX_DISCRETE_INPUTS ||
		startaddr + quantity > MODBUS_MAX_DISCRETE_INPUTS)
	{
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_ADDRESS);
	}
	
	//���������� ���� ���������� ������ ������, 1 ���� - ��������� 8�� �������
	//quantity_of_bytes = ceil((float)quantity/8);

	quantity_of_bytes = BYTES_CEILING(quantity);
	

	//������� ��������� ������ ������� � ���������

	callbacks.getdiscreteinputs_callback(regbuf,startaddr,quantity);
	
	//������������ ��������� ���������
	mbaplen = quantity_of_bytes+3;
	MakeTCPBytes(&mbap[4],mbaplen);


	//� ������ ����� ��� ������� - ��� ���������

	//������ ���� - ����� ����������� ������ ���������
	frame[1] = quantity_of_bytes;
	//���������� ����� - ����� ��������� ���������
	memcpy(&frame[2],regbuf,quantity_of_bytes);
	return mbaplen - 1;
}
/*
	������ ������� 16�������� ���������
*/
char ModbusReadInputRegisters(char* mbap, char* frame)
{
	//�����, ������������ � ������ MBAP = ����� ������ + ID slave
	char mbaplen;
	//����� ������� ���������
	char coilsbuf[MODBUS_MAX_INPUT_REGISTERS*2];
	//����� ��� BigEndian ��������
	char coilsbuf_swp[MODBUS_MAX_INPUT_REGISTERS*2];
	//���������� ������ ��������� ��������� � �������� ���������
	char quantity_of_bytes;

	//��������� ��������� �������

	//��������� �����
	u_short startaddr = GetTCPShort(&frame[1]);
	//���-�� ������������� ���������
	u_short quantity = GetTCPShort(&frame[3]);

	//����������� �������� ��������� ������� �� ���������� ��������
	if(startaddr > MODBUS_MAX_INPUT_REGISTERS ||
		startaddr + quantity > MODBUS_MAX_INPUT_REGISTERS)
	{
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_ADDRESS);
	}
	
	//���������� ���� ���������� ������ ������, 2 ����� - 1 �������
	quantity_of_bytes = quantity << 1;

	//������� ��������� ������ ������� � ���������

	callbacks.getinputregisters_callback(coilsbuf,startaddr,quantity);

	//��������������� �������� 16������ ������ � ������ BigEndian 
	//��� �������� �� ����
	_swab(coilsbuf,coilsbuf_swp,quantity_of_bytes);

	//������������ ��������� ���������
	mbaplen = quantity_of_bytes+3;
	MakeTCPBytes(&mbap[4],mbaplen);

	//� ������ ����� ��� ������� - ��� ���������

	//������ ���� - ����� ����������� ������ ���������
	frame[1] = quantity_of_bytes;
	//���������� ����� - ����� ��������� ���������
	memcpy(&frame[2],coilsbuf_swp,quantity_of_bytes);
	return mbaplen - 1;
}
/*������� modbus
	0x05 - ������ ������ ���������� ����� ���������

	������ ���� ��������� (�����):
		0	-	0�05
		1,2		-	�����
		3,4		-	�������� ( 0xFF ��� 0)
*/
char ModbusWriteSingleCoilFunc(char* mbap, char* frame)
{
	//��������� ��������� �������

	//����
	u_short coil = GetTCPShort(&frame[1]);
	//������������ ��������
	u_short value = GetTCPShort(&frame[3]);

	//������������ ��������
	if(value != 0xFF && value != 0)
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_VALUE);
	//����� ����� ������� �� ���������� ��������
	if(coil >= MODBUS_MAX_COILS)
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_ADDRESS);

	//������� ��������� ������ ������� � ���������
	callbacks.writesinglecoil_callback(coil,value);
	//� ������ ����������� ���������� ��������� ������� �����
	//������������ ����� �������, ���������� ���� ������ = ���-�� ���� �������
	return GET_PDU_LEN(mbap) - 1;
}

/*������� modbus
	0x2B, MEI 0x0E - ������������� ����������
	������ ���� ��������� (�����):
		0 - 0�2�  - �������
		1 - 0�0�  - MEI 
		2 - ������ � ������� ��������:
			01 - ������� ������������� (������������ ��� �������)
			04 - ������������� ������������� (������������ 1 ������)
		3 - ID �������
			0 - ���� ������� �������������
			0 - 2 - ���� ������������� �������������
*/
char ModbusReadDeviceIdentificationFunc(char* mbap, char* frame)
{
	//�����, ������������ � ������ MBAP = ����� ������ + ID slave
	char mbaplen;

	//������ ������ MODBUS_MAX_PDU - ���� ������� - ���� MEI
	char buffer[MODBUS_MAX_PDU - 2];

	//������ � ������� ��������
	char readDevID = frame[2];
	//ID �������
	char objectID = frame[3];

	if(frame[1] != 0x0E)
		return 0;
	//�������� Object ID �� ��������� �����������
	if(objectID > 2)
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_ADDRESS);
	if(readDevID != 1 && readDevID != 4)
		return ModbusExceptionRsp(mbap,frame,MODBUS_ILLEGAL_DATA_VALUE);
	
	mbaplen = callbacks.getdeviceid_callback(buffer,readDevID,objectID);

	return mbaplen - 1;
}

char ModbusExceptionRsp(char* mbap, char* frame,enum MODBUS_ERRORS err)
{
	MakeTCPBytes(&mbap[4],3);
	frame[0] |= 0x80;
	frame[1] = err;
	return 2;
}