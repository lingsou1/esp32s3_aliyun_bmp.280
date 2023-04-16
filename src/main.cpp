/*
接线说明:(BMP280)
        VCC -- 3.3V
        SCL -- 13
        SDO -- 12
        SDA -- 11
        CSB -- 10

程序说明:BMP280测量气压,海拔,温度数据,并将测量的数据存储在闪存中
        同时支持将测量的温度,海拔,气压数据通过MQTT服务上传至然也服务器,实现可以在手机上查看数据

注意事项:loop()循环中的MQTT任务和闪存文件任务是不能同进行的,要实现同时进行应该要使用freertos吧

函数示例:无

作者:灵首

时间:2023_3_30

*/


#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Ticker.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>


//BMP_280 宏定义
#define BMP_SCK  (13) //SCL
#define BMP_MISO (12) //SDO
#define BMP_MOSI (11) //SDA
#define BMP_CS   (10) //CSB

//对BMP280创建一个对象
Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK);

//闪存文件名及地址
String file_name = "/data/bmp280_data.txt";

//用来控制测量次数的参数,i为多少就测量多少次(使用闪存文件时)
int i=100;

//指定mqtt服务器
const char* mqttServer = "test.ranye-iot.net";

//用来计数的,在MQTT服务中实现3秒发布一次信息
Ticker ticker;
int count;

//建立WiFiMulti 的对象,对象名称是 wifi_multi
WiFiMulti wifi_multi;  //建立WiFiMulti 的对象,对象名称是 wifi_multi

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

//程序测试用
int s=10;


/*
# brief 初始化BMP280
# param 无
# retval 无
*/
void bmp280_init(){
  Serial.print("BMP280  ready!!!\n");

  unsigned status;
  status = bmp.begin();
  //status = bmp.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID);

  //检测BMP280是否成功与开发板连接
  if (!status) {
  Serial.println(F("Could not find a valid BMP280 sensor, check wiring or "
                      "try a different address!"));
  Serial.print("SensorID was: 0x"); Serial.println(bmp.sensorID(),16);
  Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
  Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
  Serial.print("        ID of 0x60 represents a BME 280.\n");
  Serial.print("        ID of 0x61 represents a BME 680.\n");
  while (1) delay(10);
  }

  /* Default settings from datasheet. (从数据手册中的默认设置)*/
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode.(操作模式) */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling(温度过采样) */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling(压力过采样) */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering.(滤波) */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time.(待机时间) */
}



/*
# brief 实时串口输出BMP280的测量数据
# param 无
# retval 无
*/
void bmp280_data_output(){

  //bmp280温度读取
  Serial.print(F("Temperature = "));
  Serial.print(bmp.readTemperature());
  Serial.println(" *C\n");

  //bmp280气压读取
  Serial.print(F("Pressure = "));
  Serial.print(bmp.readPressure());
  Serial.println(" Pa\n");

  //bmp280海拔高度读取
  Serial.print(F("Approx altitude = "));
  Serial.print(bmp.readAltitude(1013.25)); /* Adjusted to local forecast! */
  Serial.println(" m\n");

  delay(1000);

}




/*
# brief 对闪存文件进行格式化操作,可以清除上一次的测量数据
# param 无
# retval 无
*/
void SPIFFS_Format_init(){
  Serial.print("SPIFFS format begin\n");
  SPIFFS.format();    //文件格式化的函数
  Serial.print("SPIFFS format finshed !!!\n");

}



/*
# brief 启动SPIFFS
# param 无
# retval 无
*/
void SPIFFS_start_init(){
  if(SPIFFS.begin()){
    Serial.print("SPIFFS Start!!!");
    Serial.print("\n");
  }
  else{
    Serial.print("SPIFFS Failed to start!!!");
    Serial.print("\n");
  }
}



/*
# brief 检查在指定位置是否有对应的文件
# param 无
# retval 无
*/
void SPIFFS_document_scan(void){
  if(SPIFFS.exists(file_name)){
    Serial.print("SPIFFS document exists!!!\n");
  }
  else{
    Serial.print("SPIFFS document don't exists!!!\n");
  }

}



/*
# brief  向闪存文件中写入读取的数据
# param  int flag :设置的标志变量,根据输入的不同,向闪存文件写入不同的字符
          (1则写入Temperature:  ;2则写入Pressure:  ;3则写入Altitude: ;其他的数字则写入空行)
# param  float data :传感器测量的数据
# retval 无
*/
void SPIFFS_data_writing(int flag,float data){
  File data_file = SPIFFS.open(file_name,"a");
  if (flag == 1){
    data_file.println("Temperature: ");
  }
  else if (flag == 2)
  {
    data_file.println("Pressure:  ");
  }
  else if (flag == 3){
    data_file.println("Altitude:  ");
  }
  // else{
  //   data_file.println("\n");
  // }
  //写入传感器数据
  data_file.println(data);
  data_file.close();
}


/*
# brief 用来在MQTT服务中实现计数来实现3秒发布一次信息
# param 无
# retval 无
*/
void tickerCount(){
  count++;
}



/*
# brief 连接MQTT服务器,同时可以生成客户端ID
# param
# retval
*/
void connectMQTTServer(){
  // 根据ESP32s3的MAC地址生成客户端ID（避免与其它客户端ID重名）
  String clientId = "esp32s3-" + WiFi.macAddress();

  // 连接MQTT服务器
  if (mqttClient.connect(clientId.c_str())) { 
    Serial.println("MQTT Server Connected.\n");
    Serial.println("Server Address: ");
    Serial.println(mqttServer);
    Serial.print("\n");
    Serial.println("ClientId:");
    Serial.println(clientId);
    Serial.print("\n");
  } else {
    Serial.print("MQTT Server Connect Failed. Client State:");
    Serial.println(mqttClient.state());
    Serial.print("\n");
    delay(3000);
  }   
}


/*
# brief   MQTT发布信息的操作(发布的信息是关于BMP280的测量数据)
# param int flag :根据这个值来实现发布不同的内容
        (1是发送Temperature: ;2是发送Pressure:  ;3是发送Altitude:  )
# param  float data:这是BMP280的测量数据
# retval 无
*/
void pubMQTTmsg(int flag,float data){
  //这是发布的信息
  String messageString;
  // 建立发布主题。主题名称以Ling-Sou-Pub-为前缀，后面添加设备的MAC地址。
  // 这么做是为确保不同用户进行MQTT信息发布时，ESP32s3客户端名称各不相同，
  String topicString = "Ling-Sou-Pub-" + WiFi.macAddress();
  char publishTopic[topicString.length() + 1];  
  strcpy(publishTopic, topicString.c_str());

  //建立发布信息。信息内容根据flag的不同更改,实现发布温度,气压,海拔数据
  if(flag==1){
    messageString = "Temperature: " + String(data) + "℃"; 
  }
  else if(flag == 2){
    messageString = "Pressure:  " + String(data/1000) + "KPa"; 
  }
  else if(flag == 3){
    messageString = "Altitude:  " + String(data) + "m"; 
  }

  char publishMsg[messageString.length() + 1];   
  strcpy(publishMsg, messageString.c_str());

  // 实现ESP32s3向主题发布信息
  if(mqttClient.publish(publishTopic, publishMsg)){
    Serial.println("Publish Topic:");Serial.println(publishTopic);
    Serial.print("\n");
    Serial.println("Publish message:");Serial.println(publishMsg); 
    Serial.print("\n");   
  } else {
    Serial.println("Message Publish Failed.\n"); 
  }
}



/*
# brief 连接WiFi的函数
# param 无
# retval 无
*/
void wifi_multi_con(void){
  int i=0;
  while(wifi_multi.run() != WL_CONNECTED){
    delay(1000);
    i++;
    Serial.print(i);
    Serial.print(" - ");
  }
  Serial.print("wifi connected!!\n");

}


/*
# brief 写入自己要连接的WiFi名称及密码,之后会自动连接信号最强的WiFi
# param 无
# retval  无
*/
void wifi_multi_init(void){
  wifi_multi.addAP("haoze2938","12345678");
  wifi_multi.addAP("LINGSOU12","123455678");
  wifi_multi.addAP("LINGSOU1","123455678");
  wifi_multi.addAP("LINGSOU234","12345678");   //通过 wifi_multi.addAP() 添加了多个WiFi的信息,当连接时会在这些WiFi中自动搜索最强信号的WiFi连接
}



void setup() {
  //串口设置
  Serial.begin(9600);
  Serial.print("serial is OK!!\n");

  //BMP280初始化
  bmp280_init();

  //闪存文件格式化
  SPIFFS_Format_init();

  //闪存文件初始化
  SPIFFS_start_init();

  //建立文件,必须现在建立一个,不然在循环中无法对指定文件添加内容
  File dataFile = SPIFFS.open(file_name,"w");   // 建立File对象用于向SPIFFS中的file对象（即/data/bmp280_data.txt）写入信息
  dataFile.println("this is bmp's data: \n!!!");   //闪存具体的写入内容(向dataFile写入字符串信息)
  dataFile.close();   //关闭文件,打开文件进行需要的操作后要及时关闭文件

  //WiFi设置
  wifi_multi_init();
  wifi_multi_con();

  //对指定MQTT服务器建立连接
  mqttClient.setServer(mqttServer,1883);

  //连接MQTT服务器
  connectMQTTServer();

  // Ticker定时对象
  ticker.attach(1, tickerCount);
}



void loop() {
  //串口输出温度,气压,高度数据
  //bmp280_data_output();

  //获取BMP280的测量参数
  float temp = bmp.readTemperature();
  float pre = bmp.readPressure();
  float alt = bmp.readAltitude();
  
  //测量
  if (i > 0){
    //向闪存文件写入测量数据
    SPIFFS_data_writing(1,temp);
    SPIFFS_data_writing(2,pre);
    SPIFFS_data_writing(3,alt);
    //换行,但是好像不用,没搞清楚他是怎么添加的
    File data_file = SPIFFS.open(file_name,"a");
    data_file.println("\n");
    //控制测量次数
    i = i-1;
    //适当延时100ms
    delay(100);
  }


  //通过串口告知测量结束
  if (i == 0){
    Serial.print("data writing successfully!!!\n");
    i = i-1;
  }

  //通过串口输出闪存文件中存储的内容
  if(i == -1){

    //检查是否存在闪存文件
    SPIFFS_document_scan();

    //输出闪存文件内容
    File dataFile = SPIFFS.open(file_name,"r"); //建立File对象用于从SPIFFS中读取文件
    for (int i = 0; i < dataFile.size(); i++)
    {
      Serial.print((char)dataFile.read());    //不加(char) 会输出ASCII码
    }

    //结束循环
    i = i-1;
  }


  //连接MQTT服务器并发布消息
  if (mqttClient.connected()) { 
    // 如果开发板成功连接服务器
    // 每隔3秒钟发布一组信息
    if (count >= 3){
      pubMQTTmsg(1,temp);
      pubMQTTmsg(2,pre);
      pubMQTTmsg(3,alt);
      count = 0;
    }    
    // 保持心跳
    mqttClient.loop();
  } else {                  // 如果开发板未能成功连接服务器
    connectMQTTServer();    // 则尝试连接服务器
  }

}
