// code = utf-8
#ifndef __CC3200R1M1RGC__
// Do not include SPI for CC3200 LaunchPad
#include <SPI.h>
#endif
#include <WiFi.h>
#include <WiFiClient.h>

char ap_ssid[] = "NUEDC2022eB";
char sta_ssid[] = "NUEDC2022eA";
char wifi_passwd[] = "20221017";

#define board_port 8939
#define app_port 1357
#define pc_port 8809

WiFiClient board_client, app_client, pc_client;
WiFiServer board_server(board_port), app_server(app_port), pc_server(pc_port);

IPAddress board_server_ip(192, 168, 1, 1);
IPAddress board_client_ip(192, 168, 1, 2);
IPAddress board_client_subnet(255, 255, 255, 0);
IPAddress board_client_dns(114, 114, 114, 114);

bool alreadyConnected_app, alreadyConnected_board, alreadyConnected_pc;
uint8_t oldCountClients, countClients;

uint8_t ledDisp, ledDisp2 = 0xff, swRead, swRead2 = 0xff;

uint8_t frameIdx = 0;

#define pinNum 8

const uint8_t swPins[pinNum] = {3, 2, 5, 6, 7, 8, 27, 11};
const uint8_t ledPin[pinNum] = {28, 14, 15, 17, 18, 19, 31, 32};

const uint8_t modeKeyPin = 30;
const uint8_t modeLedPin = 29;
uint8_t modeLedBlink;

#define adcValNum 512
const uint8_t adcPin = 24;
uint16_t adcVal[adcValNum];
float adcF[adcValNum];

const size_t AppSendDataNum = adcValNum * 2 + 2;
uint8_t AppSendData[AppSendDataNum];

typedef enum
{
    ModeOff,
    ModeClient,
    ModeAP,
} WiFi_mode;

WiFi_mode w_mode = ModeOff;

void setup()
{
    Serial.begin(115200);

    /* gpio */
    for (uint8_t i = 0; i < pinNum; i++)
    {
        pinMode(ledPin[i], OUTPUT);
        pinMode(swPins[i], INPUT_PULLUP);
        digitalWrite(ledPin[i], 1);
    }
    pinMode(modeKeyPin, INPUT_PULLUP);
    pinMode(modeLedPin, OUTPUT);

    pinMode(adcPin, INPUT);
    while (ADCFIFOLvlGet(ADC_BASE, ADC_CH_3))
        ADCFIFORead(ADC_BASE, ADC_CH_3);
    PinTypeADC(PIN_60, 0xff);
    ADCChannelEnable(ADC_BASE, ADC_CH_3);
    ADCTimerConfig(ADC_BASE, 0x1ffff);
    ADCTimerEnable(ADC_BASE);

    Serial.println("B init ok");
    delay(10);
}

void loop()
{
    /* get data */
    if (w_mode != ModeOff)
    {
        swRead = 0;
        for (uint8_t i = 0; i < pinNum; i++)
            swRead |= (digitalRead(swPins[i])) << i;
        if (swRead2 != swRead)
        {
            Serial.print("SD = ");
            Serial.println(swRead, BIN);
        }

        ADCEnable(ADC_BASE);
        for (uint16_t i = 0; i < adcValNum; i++)
        {
            while (!ADCFIFOLvlGet(ADC_BASE, ADC_CH_3))
                ;
            adcVal[i] = ADCFIFORead(ADC_BASE, ADC_CH_3);
        }
        ADCDisable(ADC_BASE);

        for (uint16_t i = 0; i < adcValNum; i++)
        {
            adcVal[i] = (adcVal[i] & 0x3fff) >> 2;
            adcF[i] = adcVal[i] * 1.4 / 4095;
        }
    }

    /* mode change */
    if (w_mode != ModeAP && !digitalRead(modeKeyPin))
    {
        w_mode = ModeAP;
        modeLedBlink = 1;
        digitalWrite(modeLedPin, modeLedBlink);
        openAP();
    }
    else if (w_mode != ModeClient && digitalRead(modeKeyPin))
    {
        w_mode = ModeClient;
        digitalWrite(modeLedPin, LOW);
        openSTA();
    }

    if (w_mode == ModeAP)
    {
        modeLedBlink = modeLedBlink ? 0 : 1;
        digitalWrite(modeLedPin, modeLedBlink);
    }

    /* loop */
    if (w_mode == ModeAP)
        loopAP();
    else if (w_mode == ModeClient)
        loopSTA();

    /* led data display */
    if (w_mode != ModeOff && ledDisp2 != ledDisp)
        for (uint8_t i = 0; i < pinNum; i++)
            digitalWrite(ledPin[i], !(ledDisp & (1 << i)));

    /* fresh data */
    if (w_mode != ModeOff)
    {
        ledDisp2 = ledDisp;
        swRead2 = swRead;
    }
}

void openAP()
{
    Serial.print("Starting AP");
    WiFi.disconnect();
    WiFi.beginNetwork(ap_ssid, wifi_passwd);
    while (WiFi.localIP() == INADDR_NONE)
    {
        Serial.print(".");
        delay(100);
    }
    Serial.println("\nDone");
    Serial.print("SSID = ");
    Serial.println(ap_ssid);
    Serial.print("Password = ");
    Serial.println(wifi_passwd);

    // Board Server
    board_server.begin();
    Serial.print("Board Server Opened in ");
    Serial.println(board_port);

    // App Server
    app_server.begin();
    Serial.print("App Server Opened in ");
    Serial.println(app_port);

    // PC Server
    pc_server.begin();
    Serial.print("PC Server Opened in ");
    Serial.println(pc_port);
}

void loopAP()
{

    /* fresh the clients */
    countClients = WiFi.getTotalDevices();
    if (countClients != oldCountClients)
    {
        if (countClients > oldCountClients)
        {
            Serial.println("Client connected to AP");
            for (uint8_t i = 0; i < countClients; i++)
            {
                Serial.print("Client #");
                Serial.print(i);
                Serial.print(" at IP address = ");
                Serial.print(WiFi.deviceIpAddress(i));
                Serial.print(", MAC = ");
                Serial.println(WiFi.deviceMacAddress(i));
                delay(10);
            }
        }
        else
        {
            Serial.println("Client disconnected from AP.");
            alreadyConnected_board = 0;
            alreadyConnected_app = 0;
            alreadyConnected_pc = 0;
        }
        oldCountClients = countClients;
    }

    /* board comm. */
    board_client = board_server.available();
    if (board_client)
    {
        // init
        if (!alreadyConnected_board)
        {
            board_client.flush();
            Serial.println("\nBoard Client");
            alreadyConnected_board = 1;
        }

        // Rx
        if (board_client.available())
        {
            board_client.read(&ledDisp, 1);
            board_client.flush();
            if (ledDisp2 != ledDisp)
            {
                Serial.print("RD = ");
                Serial.println(ledDisp, BIN);
                delay(10);
            }
        }
        // Tx
        if (swRead2 != swRead)
        {
            board_client.write(swRead);
            delay(10);
        }
    }

    /* app comm. */
    app_client = app_server.available();
    if (app_client)
    {
        // init
        if (!alreadyConnected_app)
        {
            app_client.flush();
            Serial.println("\nApp Client");
            alreadyConnected_app = 1;
        }

        // Rx
        if (app_client.available())
        {
        }

        // Tx
        AppSendData[0] = swRead;
        AppSendData[1] = frameIdx++;
        memcpy(AppSendData + 2, adcVal, adcValNum * 2);
        for (uint16_t i = 2; i < AppSendDataNum; i += 2)
        {
            uint8_t tmp = AppSendData[i];
            AppSendData[i] = AppSendData[i + 1];
            AppSendData[i + 1] = tmp;
        }

        app_client.write(AppSendData, AppSendDataNum);

        delay(500);
    }

    /* pc comm. */
    pc_client = pc_server.available();
    if (pc_client)
    {
        // init
        if (!alreadyConnected_pc)
        {
            pc_client.flush();
            Serial.println("\nPC Client");
            alreadyConnected_pc = 1;
        }

        // Rx
        if (pc_client.available())
        {
        }

        // Tx
        for (uint16_t i = 0; i < adcValNum; i++)
        {
            pc_client.print(adcF[i]);
            pc_client.write('\n');
        }

        delay(500);
    }
}

void openSTA()
{
    Serial.print("Connecting to the network");
    WiFi.begin(sta_ssid, wifi_passwd);
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(100);
    }
    WiFi.config(board_client_ip, board_client_dns, board_server_ip, board_client_subnet);

    Serial.println("\nConnected to the network");
    printWifiStatus();

    Serial.print("Connecting to the server");
    while (!board_client.connect(board_server_ip, board_port))
    {
        Serial.print(".");
        delay(100);
    }
    Serial.println("\nConnected to the server");
}

void loopSTA()
{
    // Tx
    if (swRead2 != swRead)
    {
        board_client.write(swRead);
        delay(10);
    }

    // Rx
    if (board_client.available())
    {
        board_client.read(&ledDisp, 1);
        board_client.flush();
        if (ledDisp2 != ledDisp)
        {
            Serial.print("RD = ");
            Serial.println(ledDisp, BIN);
            delay(10);
        }
    }
}

void printWifiStatus()
{
    Serial.print("Network Name: ");
    Serial.println(WiFi.SSID());

    IPAddress wifiData = WiFi.localIP();
    Serial.print("Local IP: ");
    Serial.println(wifiData);

    wifiData = WiFi.subnetMask();
    Serial.print("Subnet Mask: ");
    Serial.println(wifiData);

    wifiData = WiFi.gatewayIP();
    Serial.print("Gateway IP: ");
    Serial.println(wifiData);

    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
}
