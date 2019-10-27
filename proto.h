#include <Arduino.h>
#include <EEPROM.h>
#include <TaskScheduler.h>
#include <SPI.h>
#include <assert.h>
#include <time.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

#define DEVICE_NAME_SIZE 10
#define RADIO_ADDRESS_LEN 3
#define RADIO_SEND_WAITING 500
#define MASTER_CONFIG_ADDRESS ((uint8_t*)"cfg")
#define MASTER_DEFAULT_ADDRESS ((uint8_t*)"svr")
#define SLAVE_BASE_ADDRESS ((uint8_t*)"cli")
#define POLL_SECONDS (60*1000)
#define MAX(a,b) (a>b?a:b)
#define MAX_PACKET_SIZE 32
#define SLAVE_NAME_MAX_LEN 10
#define SLAVE_ADDRESS_REQUEST_TIMEOUT 1000
#define SLAVE_ACTIVE_TIMEOUT 30000
#define MASTER_RADIO_ID 10
#define DEVICE_TEXT_MAX_SIZE 28
#define AVR_TO_PLATFORM(device_value,device_type) 
#define PLATFORM_TO_AVR(device_value,device_type) 
#define CREATE_SEND_ADDRESS(buf_name,slaveId) \
	uint8_t buf_name[RADIO_ADDRESS_LEN];\
	memcpy(buf_name,rx_address,RADIO_ADDRESS_LEN);\
	buf_name[0]=slaveId;

#define CREATE_TX_BUFFER(tx_pointer,tx_buffer) \
	uint8_t tx_buffer[MAX_PACKET_SIZE];\
	memset(tx_buffer,0,MAX_PACKET_SIZE);\
	uint8_t* tx_pointer=tx_buffer;

#if defined(ARDUINO_ARCH_ESP32)
class EEPROM_ESP32_Class {
	public:
		uint8_t operator[](int index) {
			return EEPROM.readByte(index);
		}

		void update(int index, uint8_t value) {
			EEPROM.writeByte(index,value);
		}
		uint8_t read(int index) {
			EEPROM.readByte(index);
		}
};

EEPROM_ESP32_Class EEPROM32;

#define EEPROM EEPROM32
#endif



/*******************************
 *
 *    Bitwise templates on enum flags
 *
 *******************************/

/*
template<typename E, class = typename E::ClearAll> inline E operator+(const E a, const E b) { return (E)((int)a|(int)b); }
template<typename E, class = typename E::ClearAll> inline E operator-(const E a, const E b) { return (E)((int)a&~(int)b); }
template<typename E, class = typename E::ClearAll> inline bool operator&(const E a, const E b) { return (bool)((int)a&(int)b); }
template<typename E, class = typename E::ClearAll> inline E& operator+=(E& a, const E b) { return (E&)((int&)a|=(int)b);}
template<typename E, class = typename E::ClearAll> inline E& operator-=(E& a, const E b) { return (E&)((int&)a&=~(int)b); }
*/
template<typename E> inline E operator+(const E a, const E b) { return (E)((int)a|(int)b); }
template<typename E> inline E operator-(const E a, const E b) { return (E)((int)a&~(int)b); }
template<typename E> inline bool operator&(const E a, const E b) { return (bool)((int)a&(int)b); }
template<typename E> inline E& operator+=(E& a, const E b) { return (E&)((int&)a|=(int)b);}
template<typename E> inline E& operator-=(E& a, const E b) { return (E&)((int&)a&=~(int)b); }

int nstrcpy(char* dest, const char*src, int maxlen) {
	int i=0;
	for(;i<maxlen;i++) 
		if(!(*(dest++)=*(src++)))
			break;
	
	return i;
}

/*******************************
 *
 *    Device base class
 *
 *******************************/

enum DeviceIO:byte {
	Input=1,
	Output=2,
} ;

enum DeviceType:byte {
	Digital=1,
	AnalogInt8=2,
	AnalogInt16=3,
	AnalogInt32=4,
	AnalogFloat=5,
	Text=8
};

union DeviceValue {
	bool digital;
	int8_t analogInt8;
	int16_t analogInt16;
	int32_t analogInt32;
	float analogFloat;
	char* text;
};

enum DeviceStatus:byte {
//	ClearAll=0,
	NewValue=1,
	FailToUpdate=2,
	OnChangeTransaction=32,
	SendOnChange=64,
	SendOnPoll=128
};


class Device;

//return true...if update ok....false to roolback only on radio=0&io=output
typedef bool (*NewValueFunction)(Device&);
int DeviceValueMinSize(DeviceType type, int size=0);

class Device {
	protected:
		static int DeviceValueMinSize(DeviceType type, int size=0) {
			DeviceValue d;
			switch (type) 			{
				case Digital: 		return sizeof(d.digital);
				case AnalogInt8: 	return sizeof(d.analogInt8);
				case AnalogInt16:	return sizeof(d.analogInt16);
				case AnalogInt32:	return sizeof(d.analogInt32);
				case AnalogFloat:	return sizeof(d.analogFloat);
				case Text:			
					assert(size<=DEVICE_TEXT_MAX_SIZE); 
					return size;
				default:
					assert(0);
			}
			return 0;
		}

		DeviceValue value;
		NewValueFunction dataFromRadio;
		DeviceStatus flags;

	public:
		const DeviceIO IO;
		const DeviceType type;
		const byte deviceId;
		const byte radioId;
		const byte size;
		const char* name; 

		Device(DeviceIO IO, DeviceType type, int deviceId, int radioId=0, const char* name={0},int size=0, NewValueFunction dataFromRadio=NULL):
			IO(IO),type(type),deviceId(deviceId),radioId(radioId),name(name),size(DeviceValueMinSize(type,size)) {
				this->dataFromRadio=dataFromRadio;
				assert(size<200);
				switch(type) {
					case Digital:        setDigital(false); break;
					case AnalogInt8:     setAnalogInt8(0); break;
					case AnalogInt16:    setAnalogInt16(0); break;
					case AnalogInt32:    setAnalogInt32(0); break;
					case AnalogFloat:    setAnalogFloat(0.0f); break;
					case Text: 
						setBuffer(malloc((size_t)size));                      
						break;
					default:
						assert(0);
				}                
			}

		~Device() { 
			if(type==Text)
				free(getBuffer());
		}

		inline DeviceStatus getSendOn() {
			return (DeviceStatus)(((uint8_t)flags)&(SendOnPoll+SendOnChange));
		}

		inline bool testFlags(DeviceStatus status) { return status&flags; }

		void setSendOn(DeviceStatus status, bool skip_eeprom=false);
		int getMasterId() const { return (int)radioId<<8|deviceId; }

		//device to radio
		uint8_t valueToRadio(uint8_t* tx) {
			if(type==Text) {
				memcpy(tx,value.text,size);
			}
			else {
				DeviceValue temp;
				memcpy(&temp,&value,size);
				PLATFORM_TO_AVR(temp,type);
				memcpy(tx,&temp,size);
			}
			return size;
		};
		
		//radio to device 
		uint8_t valueFromRadio(uint8_t* rx) {
			assert(radioId!=0||IO&Output);
			bool newdata=false;
			uint8_t backup[size];			
			if(type==Text) {
				if(memcmp(rx,value.text,size)) {
					memcpy((void*)backup,(void*)value.text,size);
					memcpy((void*)value.text,rx,size);
					newdata=true;
				}
			}
			else {
				DeviceValue temp;
				memcpy(&temp,rx,size);
				AVR_TO_PLATFORM(temp,type);
				if(memcmp(&temp,&value,size)) {
					memcpy(backup,&value,size);
					memcpy(&value,&temp,size);
					newdata=true;
				}
			}
			if(newdata) {
				if(dataFromRadio!=NULL) {
					bool done=dataFromRadio(*this);
					if(radioId==0) {
						if(done) {
							flags+=NewValue;						
						}
						else {
							flags+=FailToUpdate;
							//rollback
							if(type==Text) 
								memcpy((void*)value.text,backup,size);
							else
								memcpy(&value,backup,size);
						}
						//done by proto on write command
						//Proto::GetInstance()->sendWriteAck()
					}
				}
			}
			return size;
		}; 
		
		bool getDigital() const	{ 
			assert(type==Digital);
			return value.digital; 
		}
		int8_t getAnalogInt8() const { 
			assert(type==AnalogInt8);
			return value.analogInt8; 
		}
		int16_t getAnalogInt16() const { 
			assert(type==AnalogInt16);
			return value.analogInt16; 
		}
		int32_t getAnalogInt32() const { 
			assert(type==AnalogInt32);		
			return value.analogInt32; 
		}
		float getAnalogFloat() const { 
			assert(type==AnalogFloat);		
			return value.analogFloat; 
		}
		void* getBuffer() const	{ 
			assert(type==Text); 				
			return value.text; 
		}
		void setDigital(const bool v) { 
			assert(type==Digital);			
			if(value.digital==v) 
				return; 
			value.digital=v; 
			setNewValue();
		}
		void setAnalogInt8(const int8_t v) { 
			assert(type==AnalogInt8);			
			if(value.analogInt8==v) 
				return; 
			value.analogInt8=v; 
			setNewValue();
		}
		void setAnalogInt16(const int16_t v) { 
			assert(type==AnalogInt16);		
			if(value.analogInt16==v) 
				return; 
			value.analogInt16=v; 
			setNewValue();
		}
		void setAnalogInt32(const int32_t v) { 
			assert(type==AnalogInt32);		
			if(value.analogInt32==v) 
				return; 
			value.analogInt32=v; 
			setNewValue();
		}
		void setAnalogFloat(const float v) { 
			assert(type==AnalogFloat); 
			if(value.analogFloat==v) 
				return; 
			value.analogFloat=v; 
			setNewValue();
		}
		void copyBuffer(const void* v) { 
			assert(type==Text); 
			if(!memcmp(getBuffer(),v,size))				
				return;
			memcpy(getBuffer(),v,size); 
			setNewValue();
		}

	protected:
		void setBuffer(void* v) { 
			assert(type==Text); 
			value.text=(char*)v; 
			memset(getBuffer(),0,size); 
		}

		void setNewValue();
		void clearNewValue() 	{ flags-=NewValue; }
		void startTransaction() { flags+=OnChangeTransaction; }
		void endTransaction() 	{ flags-=OnChangeTransaction+NewValue; }
	
	friend class Proto;
};


/*******************************
 *
 *    EEPROM_HELPER
 *
 *******************************/
//[init]=EEPROM_KEY_VALUE
//[have address]=
//[master address]
//[slave id] -- solo per slave
//[n devices] -- solo se store device status
//[id][status]
//...
//[n radio] -- solo se store radio=1 e se master o fulldb
//[slave id]
//....
class ProtoEEPROM {
	protected:
		byte getSchema() { return (maxRadios<<3)^maxDevices^(saveRadios?0x0F:0xF0)^(saveDevices?0x55:0xAA)^(isMaster?0x1F:0x2E); }
		int masterAddress() { return baseAddress+2; }
		int slaveAddress() { return masterAddress()+RADIO_ADDRESS_LEN; }
		int nDeviceAddress() { return slaveAddress()+(isMaster?0:RADIO_ADDRESS_LEN); }
		int nRadioAddress() { return nDeviceAddress()+(saveDevices?1:0); }
		int deviceBaseAddress() { return nRadioAddress()+(saveRadios?1:0); }
		int radioBaseAddress() { return deviceBaseAddress()+(saveDevices?maxDevices:0); }

	public:
		const byte canary=0x55;
		bool saveRadios;
		bool saveDevices;
		bool isMaster;
		int baseAddress;
		byte maxRadios;
		byte maxDevices;

		bool isInitialized() { 
		   return EEPROM[baseAddress]==canary && EEPROM[baseAddress+1]==getSchema();
		}

		void Initialize() {
		   EEPROM.update(baseAddress,canary);
		   EEPROM.update(baseAddress+1,getSchema());
		   if(saveDevices) writeNumDevices(0);
		   if(saveRadios) writeNumRadios(0);
		}

		void Reset() {
		   EEPROM.update(baseAddress,canary+1);
		}

		void writeMasterAddress(uint8_t* address) {
		   for(int i=0;i<RADIO_ADDRESS_LEN;i++)
			   EEPROM.update(masterAddress()+i,address[i]);
		}

		void readMasterAddress(uint8_t* address) {
		   assert(isInitialized());
		   for(int i=0;i<RADIO_ADDRESS_LEN;i++)
			   address[i]=EEPROM.read(masterAddress()+i);
		}

		void writeSlaveAddress(uint8_t* address) {
			assert(!isMaster);
			for(int i=0;i<RADIO_ADDRESS_LEN;i++)
			   EEPROM.update(slaveAddress()+i,address[i]);
		}

		void readSlaveAddress(uint8_t* address) {
			assert(isInitialized());
			assert(!isMaster);
			for(int i=0;i<RADIO_ADDRESS_LEN;i++)
				address[i]=EEPROM.read(slaveAddress()+2+i);
		}

		void writeNumDevices(uint8_t numDevices) {
			assert(saveDevices);
			EEPROM.update(nDeviceAddress(),numDevices);
		}
		
		uint8_t readNumDevices() {
			assert(isInitialized());
			assert(!saveDevices);
			return EEPROM.read(nDeviceAddress());
		}

		void writeNumRadios(uint8_t numRadios) {
			assert(saveRadios);
			EEPROM.update(nRadioAddress(),numRadios);
		}
		
		uint8_t readNumRadios() {
			assert(isInitialized());
			assert(!saveRadios);
			return EEPROM.read(nRadioAddress());
		}

		void writeRadio(uint8_t idx, uint8_t slaveId) {
			assert(saveRadios);
			assert(idx<maxRadios);
			if(readNumRadios()<1+idx)
				writeNumRadios(1+idx);
			EEPROM.update(radioBaseAddress()+idx,slaveId);
		}

		uint8_t readRadio(uint8_t idx) {
			assert(isInitialized());
			assert(saveRadios);
			assert(idx<maxRadios);
			assert(readNumRadios()>idx);
			return EEPROM.read(radioBaseAddress()+idx);
		}

		void writeDevice(uint8_t idx, DeviceStatus flags) {
			assert(isInitialized());
			assert(saveDevices);
			assert(idx<maxDevices);
			if(readNumDevices()<1+idx)
				writeNumDevices(1+idx);
			EEPROM.update(deviceBaseAddress()+idx,(uint8_t)flags);
		}

		DeviceStatus readDevice(uint8_t idx) {
			assert(isInitialized());
			assert(saveDevices);
			assert(idx<maxDevices);
			assert(readNumDevices()>idx);
			return (DeviceStatus)EEPROM.read(deviceBaseAddress()+idx);
		}

};

/*******************************
 *
 *    Device indexes
 *
 *******************************/

class DeviceIndex {
	const byte maxDevices;
	byte nDevices;
	Device** devices;

	public:
		DeviceIndex(byte maxDevices):maxDevices(maxDevices) {
			nDevices=0;
			devices=(Device**)malloc(sizeof(Device*)*maxDevices);			
		}

		~DeviceIndex() {
			free((void*)devices);
		}

		Device* getDeviceByMasterId(int masterId) {
			for(int i=0;i<nDevices;i++)
				if(devices[i]->getMasterId()==masterId)
					return devices[i];
			return NULL;
		}

		Device* getDevice(int deviceId, int radioId=0) {
			return getDeviceByMasterId((int)radioId<<8|deviceId);
		}

		void addDevice(Device& device) {
			addDevice(&device);
		}

		void addDevice(Device* device) {
			assert(nDevices<maxDevices);
			assert(getDeviceByMasterId(device->getMasterId())==NULL);
			devices[nDevices++]=device;
		}

		int getNumDevices() const { return nDevices; }
		Device** getDevices() const { return devices; }

		bool checkDeviceIdIndexEq(uint8_t devId) {
			if(nDevices<devId) {
				Device* dev=devices[devId];
				return dev->deviceId==devId&&dev->radioId==0;
			}
			return false;
		}

		void readAllFromEEPROM();
		void writeAllToEEPROM();
};

/*******************************
 *
 *    Slave, Slave Index
 *
 *******************************/

enum ProtoStatus:uint16_t {
//	ClearAll=0,
	NoAddressConfigured=1, 
	NoDevicesListed=2, 
	IsConnected=4,
	IsPrimaryMaster=8,
	IsSecondaryMaster=16,
	IsSlave=32,
	HasFullDB=64,
	IsSending=128,
	IsWaitingToSend=256,
};

enum SlaveStatus:uint8_t {
//	ClearAll=0,
	AreDevicesReaded=1,
	IsActive=2,
	FullDB=4,
};

struct Slave {
	uint8_t radioId;
	SlaveStatus status;
	SlaveStatus flags;
	unsigned long last_rx;	
	unsigned long last_poll;
	int poll_millis=1000;
	char* name;
	void dataReceived() {
		status+=IsActive;
		last_rx=millis();
	}
};

class SlaveIndex {
	const byte maxSlaves;
	byte nSlaves;
	Slave** slaves;

	public:
		SlaveIndex(byte maxSlaves):maxSlaves(maxSlaves) {
			nSlaves=0;
			slaves=(Slave**)malloc(sizeof(Slave*)*maxSlaves);			
		}

		~SlaveIndex() {
			free((void*)slaves);
		}


		Slave* getSlave(int radioId) {
			for(int i=0;i<nSlaves;i++)
				if(slaves[i]->radioId==radioId)
					return slaves[i];
			return NULL;
		}

		Slave* getSlave(char* name) {
			for(int i=0;i<nSlaves;i++)
				if(!strncmp(slaves[i]->name,name,SLAVE_NAME_MAX_LEN))
					return slaves[i];
			return NULL;
		}

		uint8_t getNewRadioId() {
			int radioId=MASTER_RADIO_ID+1;
			for(int i=0;i<nSlaves;i++)
				radioId=MAX(slaves[i]->radioId,radioId);
			
			return radioId;
		}

		Slave* addSlave(int radioId, char* name) {
			assert(nSlaves<maxSlaves);
			assert(getSlave(radioId)==NULL);
			writeToEEPROM(nSlaves,radioId);
			Slave& slave=*slaves[nSlaves++];
			slave.radioId=radioId;
			slave.status=(SlaveStatus)0; //SlaveStatus::ClearAll;
			slave.name=name;
			slave.last_rx=0;
			return &slave;
		}

		void readAllFromEEPROM();
		void writeAllToEEPROM();
		void writeToEEPROM(int index, int radioId);
		int getNumSlaves() {return nSlaves; }
		Slave** getSlaves() { return slaves; }
};


/*******************************
 *
 *    Proto class
 *
 *******************************/
enum Command:uint8_t {
	cmdNewSlave=1,
	cmdConfigSlave,
	cmdSlaveOn,
	cmdDeviceListRequest,
	cmdDeviceListResponse,
	cmdDeviceListResponseEnd,
	cmdWrite,
	cmdUpdateDone,
	cmdUpdateFail,
	cmdReadRequest,
	cmdReadResponse,
	cmdPollRequest,
	cmdPollResponse,
	cmdValueChanged,
	cmdSetDeviceFlags,
};



class Proto {
	
	public:
		static Proto* GetInstance() { return instance; }
		ProtoEEPROM* getEEPROM() { return eeprom; }

	protected:
		static Proto* instance;
		ProtoEEPROM* eeprom;
		uint8_t rx_address[RADIO_ADDRESS_LEN];
		uint8_t tx_address[RADIO_ADDRESS_LEN];
		ProtoStatus flags;
		DeviceIndex deviceIndex;
		SlaveIndex slaveIndex;
		Scheduler runner;
		RF24 radio;
		Task* taskLoop;
		Task* taskKeepAlive;
		Task* taskPoll;
		char* name;

		void masterStartSend(uint8_t*sendto) {
			flags+=IsWaitingToSend;
			if(flags&IsSending) {
				while(flags&IsSending)
					delayMicroseconds(200);
			}
			else {
				radio.stopListening();
				radio.closeReadingPipe(0);
			}
			flags+=IsSending;
			flags-=IsWaitingToSend;
			radio.openWritingPipe(sendto);
		}

		void masterSend(uint8_t* tx) { 
			radio.startFastWrite(tx,MAX_PACKET_SIZE,0); 
			radio.txStandBy(RADIO_SEND_WAITING);
		}

		void masterEndSend() {
			flags-=IsSending;
			if(flags&IsWaitingToSend) {
				radio.openReadingPipe(0,MASTER_CONFIG_ADDRESS);
				radio.startListening();
			}
		}

		void slaveStartSend() {
			flags+=IsWaitingToSend;
			if(flags&IsSending) {
				while(flags&IsSending)
					delayMicroseconds(200);
			}
			else {
				radio.stopListening();
				if(flags&IsPrimaryMaster)
					radio.closeReadingPipe(0);
			}
			flags+=IsSending;
			flags-=IsWaitingToSend;
			if(flags&IsPrimaryMaster)
				radio.openWritingPipe(rx_address);
		}

		void slaveSend(uint8_t* tx) { 
			radio.startFastWrite(tx,MAX_PACKET_SIZE,0); 
			radio.txStandBy(RADIO_SEND_WAITING);
		}

		void slaveEndSend() {
			flags-=IsSending;
			if(!(flags&IsWaitingToSend)) {
				if(flags&IsPrimaryMaster)
					radio.openReadingPipe(0,MASTER_CONFIG_ADDRESS);
				radio.startListening();
			}
		}

   public:				

		void setEEPROM(ProtoEEPROM& ee) {
			eeprom=&ee;
			
			if(!flags&IsPrimaryMaster&&!flags&HasFullDB)
				eeprom->saveRadios=false;

			if(!eeprom->isInitialized()) {
				eeprom->Initialize();
				eeprom->writeMasterAddress(flags&IsPrimaryMaster?rx_address:tx_address);
				if(!flags&IsPrimaryMaster) 
					eeprom->writeSlaveAddress(rx_address);
				if(eeprom->saveRadios) 
					slaveIndex.writeAllToEEPROM();
				if(eeprom->saveDevices)
					deviceIndex.writeAllToEEPROM();
			}
			else {
				if(flags&IsPrimaryMaster) {
					eeprom->readMasterAddress(rx_address);
				}
				else {
					eeprom->readMasterAddress(tx_address);
					eeprom->readSlaveAddress(rx_address);
				}
				if(eeprom->saveRadios) 
					slaveIndex.readAllFromEEPROM();
				if(eeprom->saveDevices)
					deviceIndex.readAllFromEEPROM();
			}
		}

		void setSlaveAddress(uint8_t* address) {
			assert(!flags&IsPrimaryMaster);
			memcpy(rx_address,address,RADIO_ADDRESS_LEN);
			if(eeprom!=NULL) {
				eeprom->writeSlaveAddress(rx_address);				
			}
		}

		void setMasterAddress(uint8_t* address) {
			//forza sempre il il radio id del master
			address[0]=MASTER_RADIO_ID;
			memcpy(flags&IsPrimaryMaster?rx_address:tx_address,address,RADIO_ADDRESS_LEN);
			if(eeprom!=NULL) {
				eeprom->writeMasterAddress(address);				
			}
		}
		//outside radio Setup
		//speed
		//chanell
		//add devices
		void setup() {

			radio.setAddressWidth(RADIO_ADDRESS_LEN);
			//set ack to true
			//set tx delay .... specifico per architettura
			if(flags&IsPrimaryMaster) {
				radio.openReadingPipe(1,rx_address);
				radio.openReadingPipe(0,MASTER_CONFIG_ADDRESS);
			}
			else {
				flags+=NoDevicesListed;
				if(memcmp(tx_address,MASTER_CONFIG_ADDRESS,RADIO_ADDRESS_LEN)==0) {
					flags+=NoAddressConfigured;
				}
				else if(memcmp(tx_address+1,rx_address+1,RADIO_ADDRESS_LEN-1)!=0) {
					//indirizzo master e slave non compatibili.... resetto gli indirizzi
					for(int i=0;i<RADIO_ADDRESS_LEN;i++)
						rx_address[i]=(uint8_t)rand();
					memcpy(tx_address,MASTER_CONFIG_ADDRESS,RADIO_ADDRESS_LEN);
					flags+=NoAddressConfigured;
				}
				else {
					flags-=NoAddressConfigured;
				}
				radio.openWritingPipe(tx_address);
				radio.openReadingPipe(1,rx_address);
			}
			radio.startListening();
			
			//tskPoll=new Task(POLL_SECONDS*TASK_SECOND,TASK_FOREVER,(TaskCallback) [](){ Proto::instance->taskPoll();},&runner,true);
			//taskLoop=new Task(TASK_IMMEDIATE,TASK_FOREVER,(TaskCallback) [](){ Proto::instance->loop(); },&runner,true);
			taskLoop=new Task(100,TASK_FOREVER,(TaskCallback) [](){ Proto::instance->loop(); },&runner,true);
			if(flags&IsPrimaryMaster) {
				taskPoll=new Task(100,TASK_FOREVER,(TaskCallback) [](){ Proto::instance->poll(); },&runner,true);
			}
			taskKeepAlive=new Task(60000,TASK_FOREVER,(TaskCallback) [](){ Proto::instance->keepAlive(); },&runner,true);
		}

		void keepAlive() {
			/*
			int n=slaveIndex.getNumSlaves();
			for(int i=0;i<n;i++) {
				Slave* slave=slaveIndex.getSlaves()[i];
				if(slave->flags&IsActive)
					if(millis()-slave->last_poll>slave->poll_millis) 
						sendPollRequest(slave->radioId);
			}
			*/
		}

		void poll() {
			int n=slaveIndex.getNumSlaves();
			for(int i=0;i<n;i++) {
				Slave* slave=slaveIndex.getSlaves()[i];
				if(slave->flags&IsActive)
					if(millis()-slave->last_poll>slave->poll_millis) 
						sendPollRequest(slave->radioId);
			}
		}

		//loop for radio comunication
		void loop() {

			if(flags&IsPrimaryMaster) {
				int n=slaveIndex.getNumSlaves();
				for(int i=0;i<n;i++) {
					Slave* slave=slaveIndex.getSlaves()[i];
					if(slave->flags&IsActive)
						if(millis()-slave->last_rx>SLAVE_ACTIVE_TIMEOUT)
							slave->flags-=IsActive; 
				}
			}
			else {
				static unsigned long last_ping=0;
				if(flags&NoAddressConfigured) {
					if(millis()-last_ping>SLAVE_ADDRESS_REQUEST_TIMEOUT) {
						last_ping=millis();
						sendNewSlave();
						delay(50);
					}
				} else if(flags&NoDevicesListed) {
					if(millis()-last_ping>SLAVE_ADDRESS_REQUEST_TIMEOUT) {
						last_ping=millis();
						sendSlaveOn();
						delay(50);
					}
				}
			}


			uint8_t pipe;
			uint8_t rx_buf[MAX_PACKET_SIZE];
			uint8_t* rx;
			while(radio.available(&pipe)) {
				rx=rx_buf;
				radio.read(rx,MAX_PACKET_SIZE);
				Command cmd=(Command)*(rx++);
				switch(cmd) {
					case cmdNewSlave: 				execNewSlave(pipe,rx);				break;
					case cmdConfigSlave:			execConfigSlave(pipe,rx);			break;					
					case cmdSlaveOn:				execSlaveOn(pipe,rx);				break;
					case cmdDeviceListRequest:		execDeviceListRequest(pipe,rx);		break;
					case cmdDeviceListResponse: 	execDeviceListResponse(pipe,rx);	break;
					case cmdDeviceListResponseEnd:	execDeviceListResponseEnd(pipe,rx);	break;
					case cmdUpdateDone:
					case cmdUpdateFail:
					case cmdPollResponse:
					case cmdReadResponse:			
					case cmdWrite:					execWrite(pipe,rx,cmd);				break;
					case cmdReadRequest:			execReadRequest(pipe,rx);			break;
					case cmdPollRequest:			execPollRequest(pipe,rx);			break;
					case cmdSetDeviceFlags:			execSetDeviceFlags(pipe,rx);		break;
					default:
						assert(0);
				}
			}
		}



		Proto(RF24& radio, char* name={0}, int maxDevices=10, int maxSlaves=1,bool master=false, bool fullDB=false):
			radio(radio), name(name), deviceIndex(maxDevices),slaveIndex(maxSlaves) {

			flags=(ProtoStatus)0; //ProtoStatus::ClearAll;
			flags+= master?IsPrimaryMaster:IsSlave;
			if(fullDB) flags+=HasFullDB;
			if(master) {
				memcpy(rx_address,MASTER_DEFAULT_ADDRESS,RADIO_ADDRESS_LEN);
			}
			else {
				srand(time(NULL));
				for(int i=0;i<RADIO_ADDRESS_LEN;i++)
					rx_address[i]=(uint8_t)rand();
				memcpy(tx_address,MASTER_CONFIG_ADDRESS,RADIO_ADDRESS_LEN);
				flags+=NoAddressConfigured;
				flags+=NoDevicesListed;
			}
		};

		DeviceIndex& GetDeviceIndex() { return deviceIndex; }

		void sendNewSlave() {
			CREATE_TX_BUFFER(tx,tx_buf);
			*(tx++)=cmdNewSlave;						
			memcpy(tx,rx_address,RADIO_ADDRESS_LEN);
			tx+=RADIO_ADDRESS_LEN;
			tx+=nstrcpy((char*)tx,name,SLAVE_NAME_MAX_LEN);
			slaveStartSend();
			slaveSend(tx);
			slaveEndSend();
		}

		void execNewSlave(uint8_t pipe, uint8_t* rx) {
			CREATE_TX_BUFFER(tx,tx_buf);
			assert(pipe==0);
			assert(flags&IsPrimaryMaster);
			uint8_t* sender=rx;
			rx+=RADIO_ADDRESS_LEN;
			uint8_t radioId=0;
			size_t len=strnlen((char*)rx,SLAVE_NAME_MAX_LEN);
			Slave* slave=slaveIndex.getSlave((char*)rx);
			len=MAX(len+1,SLAVE_NAME_MAX_LEN);
			if(slave==NULL) {
				char* name=(char*)malloc(len);
				radioId=slaveIndex.getNewRadioId();
				slave=slaveIndex.addSlave(radioId,name);
			}
			else
				radioId= slave->radioId;
			rx+=len;
			sendConfigSlave(sender,radioId);
		}

		void sendConfigSlave(uint8_t* sendTo, uint8_t radioId) {
			CREATE_TX_BUFFER(tx,tx_buf);
			*(tx++)=cmdConfigSlave;						
			memcpy(tx,rx_address,RADIO_ADDRESS_LEN);
			tx+=RADIO_ADDRESS_LEN;
			memcpy(tx,rx_address,RADIO_ADDRESS_LEN);
			tx[0]=radioId;
			masterStartSend(sendTo);
			masterSend(tx_buf);
			masterEndSend();
		}

		void execConfigSlave(uint8_t pipe, uint8_t* rx) {
			assert(flags&IsSlave);
			setMasterAddress(rx);
			//memcpy(tx_address,rx,RADIO_ADDRESS_LEN);
			rx+=RADIO_ADDRESS_LEN;
			//memcpy(rx_address,rx,RADIO_ADDRESS_LEN);
			setSlaveAddress(rx);
			rx+=RADIO_ADDRESS_LEN;
			radio.stopListening();
			radio.closeReadingPipe(1);
			radio.openReadingPipe(1,rx_address);
			radio.openWritingPipe(tx_address);
			if(flags&HasFullDB)
				radio.openReadingPipe(2,tx_address);
			radio.startListening();
			flags-=NoAddressConfigured;
		}
		void sendSlaveOn() {
			if(flags&IsPrimaryMaster)
				return;
			assert(!flags&NoAddressConfigured);
			CREATE_TX_BUFFER(tx,tx_buf);
			*(tx++)=cmdSlaveOn;						
			*(tx++)=*rx_address;
			*(tx++)=flags;
			tx+=nstrcpy((char*)tx,name,SLAVE_NAME_MAX_LEN);
			slaveStartSend();
			slaveSend(tx_buf);
			slaveEndSend();
			slaveIndex.addSlave(tx_address[0],NULL);

		}
		void execSlaveOn(uint8_t pipe, uint8_t* rx) {
			if(!(flags&IsPrimaryMaster||flags&HasFullDB))
				return;

			uint8_t slaveId=*(rx++);
			Slave* slave=slaveIndex.getSlave(slaveId);
			uint8_t flags=*(rx++);
			size_t len=strnlen((char*)rx,SLAVE_NAME_MAX_LEN);
			len=MAX(len+1,SLAVE_NAME_MAX_LEN);
			if(slave==NULL)
				slave=slaveIndex.addSlave(slaveId,NULL);
			if(slave->name!=NULL&&strncmp((char*)rx,slave->name,len)!=0) {
				free(slave->name);
				slave->name=NULL;
			}
			if(slave->name==NULL) {
				char* name=(char*)malloc(len);
				nstrcpy(name,(char*)rx,len);
				slave->name=name;
			}
			rx+=len;
			if(flags&HasFullDB)
				slave->flags+=FullDB;
			else
				slave->flags-=FullDB;			
			slave->dataReceived();
			if(flags&IsPrimaryMaster)
				sendDeviceListRequest(slaveId);
		}

	protected:
		int sendDeviceValuesCmd(uint8_t slaveId, int nDevices, Device** devs, Command cmd) {
			CREATE_TX_BUFFER(tx,tx_buf);
			int i=0;

			if(flags&IsPrimaryMaster) {
				CREATE_SEND_ADDRESS(sendTo,slaveId);
				masterStartSend(sendTo);
			}
			else 
				slaveStartSend();
						
			*(tx++)=(uint8_t)cmd;
			*(tx++)=rx_address[0];
			uint8_t* last=tx++;
			*last=0;
			uint8_t* pn=tx++; 
			*pn=0;

			while(i<nDevices) {
				tx=tx_buf;
				for(;i<nDevices;i++) {
					Device* dev=devs[i];
					assert(dev!=NULL);
					assert(dev->IO==Output);
					assert(dev->radioId!=0);
					if(tx+dev->size>tx_buf+MAX_PACKET_SIZE)
						break;
					*(tx++)=dev->deviceId;
					tx+=dev->valueToRadio(tx);
					(*pn)++;
				}
				if(i==nDevices)
					*last=1;
				if(flags&IsPrimaryMaster) 
					masterSend(tx_buf);			
				else 
					slaveSend(tx_buf);
			}

			if(flags&IsPrimaryMaster) 
				masterEndSend();			
			else 
				slaveEndSend();
						
			return nDevices;
		}
	
	public:
		inline int sendWrite(uint8_t slaveId, int nDevices, Device** devs) {
			return sendDeviceValuesCmd(slaveId, nDevices, devs, cmdWrite);
		}

		inline void sendWrite(uint8_t slaveId, Device* dev) {
			sendWrite(slaveId, 1, &dev);
		}

		void sendPollRequest(uint8_t slaveId) {
			//assert(!flags&IsSlave)
			CREATE_TX_BUFFER(tx,tx_buf);
			*(tx++)=cmdPollRequest;
			*(tx++)=rx_address[0];
			if(flags&IsPrimaryMaster) {
				CREATE_SEND_ADDRESS(sendTo,slaveId);
				masterStartSend(sendTo);
				masterStartSend(tx_buf);
				masterEndSend();
			}
			else {
				slaveStartSend();
				slaveSend(tx_buf);
				slaveEndSend();
				}		
		}

		void execPollRequest(uint8_t pipe, uint8_t* rx) {
			int npoll=0;
			Device** devs=deviceIndex.getDevices();
			for(int i=0;i<deviceIndex.getNumDevices();i++)
				if(devs[i]->radioId==0&&devs[i]->testFlags(SendOnPoll+NewValue))
					npoll++;
			
			Device* poll[npoll];
			int n=0;
			for(int i=0;i<deviceIndex.getNumDevices()&&n<npoll;i++)
				if(devs[i]->radioId==0&&devs[i]->testFlags(SendOnPoll+NewValue))
					poll[n++]=devs[i];
			sendDeviceValuesCmd(tx_address[0],n,poll,cmdPollResponse);
		}

		//gestire last.... stare in ricezione fino a quando non arriva last=1
		void execWrite(uint8_t pipe, uint8_t* rx, Command cmd) {
			assert(cmd==cmdWrite||cmd==cmdUpdateFail||cmd==cmdUpdateDone||cmd==cmdReadResponse||cmd==cmdPollResponse);
			uint8_t slaveId=*(rx++);
			Slave* slave=slaveIndex.getSlave(slaveId);
			slave->dataReceived();
			if(cmd==cmdWrite) {
				slaveId=0;
			}
			int last=*(rx++);
			int n=*(rx++);
			Device* done_devs[n];
			Device* fail_devs[n];
			int done=0;
			int fail=0;
			for(int i=0;i<n;i++) {
				uint8_t deviceId=*(rx++);
				Device* dev=deviceIndex.getDevice(deviceId,slaveId);
				assert(dev!=NULL);
				if(cmd==cmdWrite&&dev->radioId==0) {
					assert(dev->IO==Output);
					dev->startTransaction();
					rx+=dev->valueFromRadio(rx);
					if(dev->testFlags(FailToUpdate))
						fail_devs[fail++]=dev;
					if(dev->testFlags(NewValue))
						done_devs[done++]=dev;
				}
				else {
					assert(!(flags&IsSlave)||flags&HasFullDB);
					rx+=dev->valueFromRadio(rx);
					if(cmd==cmdUpdateDone)
						dev->flags-=FailToUpdate;
					if(cmd=cmdUpdateFail)
						dev->flags+=FailToUpdate;
				}
			}				
			if(done>0)
				sendDeviceValuesCmd(slaveId,done,done_devs,cmdUpdateDone);
			if(fail>0)
				sendDeviceValuesCmd(slaveId,done,done_devs,cmdUpdateFail);

			for(int i=0;i<done;i++)
				done_devs[i]->endTransaction();
			for(int i=0;i<fail;i++)
				done_devs[i]->endTransaction();
		}

		void sendReadRequest(uint8_t slaveId, Device* dev) {
			sendReadRequest(slaveId,1,&dev);
		}

		void sendSetDeviceFlags(uint8_t slaveId, int nDevices, Device** devs) {
			assert(!flags&IsSlave);
			CREATE_TX_BUFFER(tx,tx_buf);
			CREATE_SEND_ADDRESS(sendTo,slaveId);
			masterStartSend(sendTo);
			int i=0;
			while(i<nDevices) {
				tx=tx_buf;
				*(tx++)=cmdSetDeviceFlags;
				*(tx++)=rx_address[0];
				uint8_t* last=tx++;
				*last=0;
				uint8_t* pn=tx++; 
				*pn=0;
				for(;i<nDevices;i++) {
					Device* dev=devs[i];
					if(dev->radioId==slaveId) {
						if(tx+2>tx_buf+MAX_PACKET_SIZE)
							break;
						*(tx++)=dev->deviceId;
						*(tx++)=dev->getSendOn();
						(*pn)++;
					}
				}
				*last=i==nDevices?1:0;
				masterStartSend(tx_buf);
			}
			masterEndSend();
		}

		void execSetDeviceFlags(uint8_t pipe, uint8_t* rx) {
			assert(flags&IsSlave);
			uint8_t sender=*(rx++);
			uint8_t last=*(rx++);
			uint8_t n=*(rx++); 
			for(int i=0;i<n;i++) {		
				uint8_t devId=*(rx++);
				uint8_t flags=*(rx++);
				Device* dev=deviceIndex.getDevice(devId);
				assert(dev!=NULL);
				dev->setSendOn((DeviceStatus)flags);
			}
		}


		int sendReadRequest(uint8_t slaveId, int nDevices, Device** devs) {
			CREATE_TX_BUFFER(tx,tx_buf);
			*(tx++)=cmdReadRequest;
			*(tx++)=rx_address[0];
			uint8_t* pn=tx++; 
			*pn=0;
			for(int i=0;i<nDevices;i++) {
				Device* dev=devs[i];
				if(tx+sizeof(dev->deviceId)>tx_buf+MAX_PACKET_SIZE)
					break;
				*(tx++)=dev->deviceId;
				(*pn)++;
			}

			if(flags&IsPrimaryMaster) {
				CREATE_SEND_ADDRESS(sendTo,slaveId);
				masterStartSend(sendTo);
				masterStartSend(tx_buf);
				masterEndSend();
			}
			else {
				slaveStartSend();
				slaveSend(tx_buf);
				slaveEndSend();
				}		
			return *pn;
		}

		void execReadRequest(uint8_t pipe, uint8_t* rx) {
			uint8_t slaveId=*(rx++);			
			Slave* slave=slaveIndex.getSlave(slaveId);
			slave->dataReceived();
			int n=*(rx++);
			Device* devs[n];
			for(int i=0;i<n;i++) {
				uint8_t deviceId=*(rx++);
				devs[i]=deviceIndex.getDevice(deviceId,slaveId);
				assert(devs[i]!=NULL);
			}
			sendDeviceValuesCmd(slaveId,n,devs,cmdReadResponse);

		}
		//anche il master deve inviare le devices... per i fulldb capire come
		void sendDeviceListRequest(uint8_t slaveId) {
			CREATE_TX_BUFFER(tx,tx_buf);
			CREATE_SEND_ADDRESS(sendTo,slaveId);
			*(tx++)=cmdDeviceListRequest;				
			*(tx++)=rx_address[0];
			//attenzione se inviato da uno slave
			masterStartSend(sendTo);
			masterSend(tx_buf);
			masterEndSend();
		}

		void sendDeviceListResponse() {
			CREATE_TX_BUFFER(tx,tx_buf);
			slaveStartSend();
			Device** devices=deviceIndex.getDevices();
			for(int i=0;i<deviceIndex.getNumDevices();i++) {
				Device& dev=*devices[i];
				if(dev.radioId==0) {				
					tx=tx_buf;
					*(tx++)=cmdDeviceListResponse;	
					*(tx++)=rx_address[0];
					*(tx++)=dev.deviceId;
					*(tx++)=dev.IO;
					*(tx++)=dev.type;
					*(tx++)=dev.size;
					tx+=nstrcpy((char*)tx,dev.name,DEVICE_NAME_SIZE);
					tx+=dev.valueToRadio(tx);
					assert(tx<=tx_buf+MAX_PACKET_SIZE);
					slaveSend(tx_buf);
				}
			}
			tx=tx_buf;
			*(tx++)=cmdDeviceListResponseEnd;	
			*(tx++)=rx_address[0];
			slaveSend(tx_buf);
			slaveEndSend();
			flags-=NoDevicesListed;
		}
		void execDeviceListResponseEnd(uint8_t pipe, uint8_t* rx) {
			if(!(flags&IsPrimaryMaster||flags&HasFullDB))
				return;

			int slaveId=*(rx++);
			Slave* slave=slaveIndex.getSlave(slaveId);
			assert(slave!=NULL);
			slave->status+=AreDevicesReaded;
			slave->dataReceived();
		}

		void execDeviceListResponse(uint8_t pipe, uint8_t* rx) {
			
			if(!(flags&IsPrimaryMaster||flags&HasFullDB))
				return;

			int slaveId=*(rx++);
			int deviceId=*(rx++);

			Slave* slave=slaveIndex.getSlave(slaveId);
			assert(slave!=NULL);
			slave->dataReceived();

			Device* dev=deviceIndex.getDevice(deviceId,slaveId);
			DeviceIO io=(DeviceIO)*(rx++);
			DeviceType type=(DeviceType)*(rx++);
			uint8_t size=*(rx++);
			char* name=(char*)rx;
			int len=strnlen(name,DEVICE_NAME_SIZE)+1;
			len=MAX(len,DEVICE_NAME_SIZE);
			rx+=len;
			if(dev==NULL) {
				char* name_buf=(char*)malloc(len);
				memcpy(name_buf,name,len);
				dev=new Device(io,type,deviceId,slaveId,name_buf,size);
				deviceIndex.addDevice(dev);
			}
			rx+=dev->valueFromRadio(rx);
		}

		void execDeviceListRequest(uint8_t pipe, uint8_t* rx) {
			//assert(sender==rx_address[0]);
			//response always to master address
			uint8_t sender=*(rx++);
			Slave* slave=slaveIndex.getSlave(sender);
			slave->dataReceived();

			if(flags&IsPrimaryMaster) {
				sendDeviceListResponse();

			} else if (flags&IsSlave) {
				sendDeviceListResponse();
			}	
		}

		void sendNewValue(Device* device) {
			//se master lo invia a se stesso cosi leggono tutti i fulldb
			sendDeviceValuesCmd(tx_address[0],1,&device,cmdValueChanged);
		}

	public:
};


/*******************************
 *
 *    CPP
 *
 *******************************/

/*******************************
 *
 *    SlaveIndex
 *
 *******************************/

void SlaveIndex::readAllFromEEPROM() {
	ProtoEEPROM* ee= Proto::GetInstance()->getEEPROM();
	assert(ee!=NULL&&ee->saveRadios);
	int n=ee->readNumRadios();
	for(int i=0;i<n;i++)
		addSlave(ee->readRadio(i),NULL);
}

void SlaveIndex::writeAllToEEPROM() {
	ProtoEEPROM* ee= Proto::GetInstance()->getEEPROM();
	assert(ee!=NULL&&ee->saveRadios);
	assert(ee->maxRadios>=nSlaves);
	//ee->writeNumRadios(nSlaves);
	for(int i=0;i<nSlaves;i++)
		ee->writeRadio(i,slaves[i]->radioId);
}
void SlaveIndex::writeToEEPROM(int index, int radioId) {
	ProtoEEPROM* ee= Proto::GetInstance()->getEEPROM();
	if(ee!=NULL&&ee->saveRadios&&ee->maxRadios>index);
	//ee->writeNumRadios(index);
	ee->writeRadio(index,radioId);
}

/*******************************
 *
 *    DeviceIndex
 *
 *******************************/

void DeviceIndex::readAllFromEEPROM() {
	ProtoEEPROM* ee= Proto::GetInstance()->getEEPROM();
	assert(ee!=NULL&&ee->saveDevices);
	int n=ee->readNumDevices();
	for(int i=0;i<n;i++) {
		Device* dev=getDevice(i,0);
		DeviceStatus status=(DeviceStatus)0; //DeviceStatus::ClearAll;
		if(i<n) {
			status=ee->readDevice(i);
			dev->setSendOn(status);
		}
	}
}

void DeviceIndex::writeAllToEEPROM() {
	ProtoEEPROM* ee= Proto::GetInstance()->getEEPROM();
	assert(ee!=NULL&&ee->saveDevices);
	//assert(ee->maxRadios>=nSlaves);
	//ee->writeNumRadios(nSlaves);
	int index=0;
	for(int i=0;i<nDevices;i++) {
		Device* dev=devices[i];
		if(dev->radioId==0&&i==index) {
			ee->writeDevice(index++,dev->getSendOn());
		}
	}
}		

/*******************************
 *
 *    Device
 *
 *******************************/

void Device::setNewValue() {
	if(radioId==0) {
		if(flags&SendOnChange&&!(flags&OnChangeTransaction))
			Proto::GetInstance()->sendNewValue(this);
		else
			flags+=NewValue;				
	}
}		

void Device::setSendOn(DeviceStatus status, bool skip_eeprom) {
	flags-=(SendOnPoll+SendOnChange);
	flags+=status;
	if(!skip_eeprom&&radioId==0) {
		Proto* p=Proto::GetInstance();
		ProtoEEPROM* ee= p->getEEPROM();
		if(ee!=NULL&&ee->saveDevices) {
			if(p->GetDeviceIndex().checkDeviceIdIndexEq(deviceId))
				ee->writeDevice(deviceId,getSendOn());
		}
	}
}

/*******************************
 *
 *    Proto
 *
 *******************************/


Proto* Proto::instance=NULL;

