//Zewnętrzna stacja pogodowa
//Autorzy: Michał Miksiewicz, Michał Pasieka
#include "PMS.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi
WiFiClient espClient;
PubSubClient client(espClient);
const char *ssid = "MikroTik-57B99B";
const char *password = "IOT_wifi";

// Wyswietlacz
#define OLED_SDA 21
#define OLED_SCL 22
Adafruit_SH1106 display(21, 22); // definiujemy piny, do których podłączony został wyświetlacz

// Czujnik czastek stalych
PMS pms(Serial);
PMS::DATA data;
float avg_pm10 = 0;
float avg_pm25 = 0;
float avg_pm100 = 0;

// Czujnik bme680
Adafruit_BME680 bme;
float pres = 0;
int hum = 0;
float temp = 0;

// Czujnik zacmienia
int analogPin = 33;
int light_val = 0;

// Flagi i zmienne odslugujace bledy
int i = 0, nd = 0;
int correct_sleep = 1800, error_sleep = 900; // w sekundach
bool PMS_ERR = false, isCheckingPM = false, isErrorSleep = false;

// MQTT Broker
const char *mqtt_broker = "10.0.2.120";
const int   mqtt_port = 443;
const char *mqtt_username = "esp32WeatherStation";
const char *mqtt_password = "public";
const char *topic0 = "WeatherStation/Light";
const char *topic1 = "WeatherStation/Pressure_hPa";
const char *topic2 = "WeatherStation/Humidity_%";
const char *topic3 = "WeatherStation/Temp_C";
const char *topic4 = "WeatherStation/PM1.0";
const char *topic5 = "WeatherStation/PM2.5";
const char *topic6 = "WeatherStation/PM10.0";

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Wiadomość dotarła: ");
    Serial.println(topic);
    Serial.print("Wiadomosc:");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
}

void PomiarBme680(int seconds)
{
    for (int c = 0; c <= seconds; c++)
    {
        pres = bme.readPressure() / 100.0F;
        hum = bme.readHumidity();
        temp = bme.readTemperature();

        light_val = analogRead(analogPin);
        light_val = map(light_val, 0, 4095, 0, 100);
        Serial.print("Zacmienie: ");
        Serial.println(light_val);

        Serial.print("Temperature = ");
        Serial.print(temp);
        Serial.println(" *C");

        Serial.print("Pressure = ");
        Serial.print((int)pres);
        Serial.println(" hPa");

        Serial.print("Humidity = ");
        Serial.print(hum);
        Serial.println(" %");
        if (!isCheckingPM)
        {
            display.clearDisplay();
            display.setTextColor(WHITE);
            display.setCursor(0, 0);
            if (!isErrorSleep)
                display.print("Pomiar PM: ");
            else
            {
                display.print("Czujnik nie odpowiada");
                display.print(error_sleep);
                display.println(" s uspienia");
            }
            display.setCursor(0, 20);
            display.print("PM1.0:  ");
            display.println(avg_pm10);
            display.print("PM2.5:  ");
            display.println(avg_pm25);
            display.print("PM10.0: ");
            display.println(avg_pm100);
            display.setCursor(0, 55);
            display.print("T:");
            display.print(temp);
            display.print(" P:");
            display.print((float)pres, 1);
            display.print(" H:");
            display.print(hum);
            display.display();
        }
        char msg_out[20];
        sprintf(msg_out, "%d", light_val);
        client.publish(topic0, msg_out);
        sprintf(msg_out, "%f", pres);
        client.publish(topic1, msg_out);
        sprintf(msg_out, "%d", hum);
        client.publish(topic2, msg_out);
        sprintf(msg_out, "%f", temp);
        client.publish(topic3, msg_out);

        client.loop();
        delay(1000);
    }
}

void setup()
{
    Serial.begin(9600);
    Wire.begin(21, 22);//inicjacja biblioteki wire (i2c)
    if (!bme.begin())//jesli 
    {
        Serial.println("Could not find a valid BME680 sensor, check wiring!");
        while (1)
            ;
    }
    Serial.println("BME680 sensor found!");
    delay(1000);
    // wifi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.println("Łączenie z WiFi...");
    }
    // mqtt
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);

    display.begin(SH1106_SWITCHCAPVCC, 0x3C); // definiujemy rodzaj użytego wyświetlacza oraz adres I2C
    pms.passiveMode();                        // Tryb pasywny w tym trybie czujnik wysyla dane tylko za żądaniem 
    display.clearDisplay();
    display.display();
}

void loop()
{
  //poloczenie z serwerem MQTT 
    while (!client.connected())
    {
        String client_id = "esp32-klient-";
        client_id += String(WiFi.macAddress());
        Serial.printf("Klient %s łączy się z publicznym brokerem MQTT\n", client_id.c_str());
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password))
        {
            Serial.println("Połączono z brokerem MQTT");
        }
        else
        {
            Serial.print("Błąd Sieci");
            Serial.print(client.state());
            delay(2000);
        }
    }
    Serial.println("Wybudzanie czujnika...");

    pms.wakeUp(); // Tryb operacyjny czujnika (wybudzenie)

    //definicja srednich odczutow z 10 pomiarów
    avg_pm10 = 0;
    avg_pm25 = 0;
    avg_pm100 = 0;
    while (true)
    {
        PomiarBme680(30); //pomiar czujnika bme680 30 razy z 1s delay
        isCheckingPM = true; //status pobierania danych 
        Serial.print("Numer pomiaru: ");
        Serial.println(i + 1);
        pms.requestRead(); //wyslanie żądania w trybie pasywnym do czujnika

        if (pms.readUntil(data)) //odczyt danych
        {
            Serial.print("\nPM 1.0 (ug/m3): ");
            Serial.println(data.PM_AE_UG_1_0);
            avg_pm10 += data.PM_AE_UG_1_0;

            Serial.print("PM 2.5 (ug/m3): ");
            Serial.println(data.PM_AE_UG_2_5);
            avg_pm25 += data.PM_AE_UG_2_5;

            Serial.print("PM 10.0 (ug/m3): ");
            Serial.println(data.PM_AE_UG_10_0);
            avg_pm100 += data.PM_AE_UG_10_0;
            
            display.clearDisplay();
            display.setTextColor(WHITE);
            display.setCursor(0, 0);
            display.println("Wykonywanie 10 probek pomiarow");
            display.setCursor(0, 15);
            display.print("Numer pomiaru: ");
            display.println(i + 1);
            display.setCursor(0, 30);
            display.print("PM1.0 : ");
            display.println(data.PM_AE_UG_1_0);
            display.print("PM2.5 : ");
            display.println(data.PM_AE_UG_2_5);
            display.print("PM10.0: ");
            display.println(data.PM_AE_UG_10_0);
            display.setCursor(0, 55);
            display.print("T:");
            display.print(temp);
            display.print(" P:");
            display.print((float)pres, 1);
            display.print(" H:");
            display.print(hum);
            display.display();

            i++;
        }
        else //jesli dane nie istnieja wyswietl ostatnie poprawne wartosci 
        {
            Serial.println("\nNo data.");
            display.clearDisplay();
            display.setTextColor(WHITE);
            display.setCursor(0, 0);
            display.println("Wykonywanie 10 probek pomiarow");
            display.setCursor(0, 15);
            display.setTextColor(WHITE);
            display.setCursor(0, 25);
            display.print("Brak pomiaru: ");
            display.println(nd + 1);
            display.setCursor(0, 55);
            display.print("T:");
            display.print(temp);
            display.print(" P:");
            display.print((float)pres, 1);
            display.print(" H:");
            display.print(hum);
            display.display();
            nd++;
        }
        if (nd >= 10) //jesli dane nie zostaly wyswietlone 10 raz z rzedu 
        {
            PMS_ERR = true; //ustaw flage bledu
            break;
        }
        if (i >= 10)
            break;
    }
    if (PMS_ERR)
    {
        isCheckingPM = false;
        Serial.println("Czujnik nie odpowiada.");
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.print("Czujnik nie odpowiada");
        display.print(error_sleep);
        display.println(" s uspienia");
        display.display();
        pms.sleep();
        isErrorSleep = true;
        PomiarBme680(error_sleep);
        isErrorSleep = false;
    }
    if (!PMS_ERR)
    {
        isCheckingPM = false;
        avg_pm10 /= 10;
        avg_pm25 /= 10;
        avg_pm100 /= 10;
        Serial.println("Średnia 10 pomiarow: ");
        Serial.println(avg_pm10);
        Serial.println(avg_pm25);
        Serial.println(avg_pm100);
        // Wyswietlacz
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.print("Pomiar PM: ");
        display.setCursor(0, 20);
        display.print("PM1.0:  ");
        display.println(avg_pm10);
        display.print("PM2.5:  ");
        display.println(avg_pm25);
        display.print("PM10.0: ");
        display.println(avg_pm100);
        display.setCursor(0, 55);
        display.print("T:");
        display.print(temp);
        display.print(" P:");
        display.print(pres);
        display.print(" H:");
        display.print(hum);
        display.display();

        char msg_out[20];
        sprintf(msg_out, "%f", avg_pm10);
        client.publish(topic4, msg_out);
        sprintf(msg_out, "%f", avg_pm25);
        client.publish(topic5, msg_out);
        sprintf(msg_out, "%f", avg_pm100);
        client.publish(topic6, msg_out);
    }

    //Reset flag i zmiennych iteracyjnych
    i = 0; 
    nd = 0;
    PMS_ERR = false;
    Serial.println("[PMS3003] Going to sleep.");
    pms.sleep();  //uspij czujnik pms3003
    PomiarBme680(correct_sleep); //oczytuj tylko czujnik bme680 przez "correct_sleep" sekund czasu
}
