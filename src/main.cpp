/*
 F4LAA : ESP32 Real Time Morse Decoder
 12/12/2023 : MorseDecoder V1.0
 
   Trouvé grace à la vidéo YouTube de G6EJD : https://www.youtube.com/watch?v=9OWl8zOHgls

   Le code ESP32 est disponible ici : https://github.com/G6EJD/ESP32-Morse-Decoder

   Carte : NodeMCU-32S

   Adaptation: 
     Utilisation de l'écran TFT 4" avec la librairie rapide TFT_eSPI
     https://github.com/Bodmer/TFT_eSPI
     ATTENTION :
       La définitions des Pins n'est pas dans ce programme, 
       ==> il faut adapter le fichier E:\Users\syst4\Documents\Arduino\libraries\TFT_eSPI-master\User_Setup.h de la librairie TFT_eSPI

 24/12/2023 : Modifications V1.0 ==> V1.2 :
   - Ajout d’un encodeur rotatif permettant de modifier manuellement les paramètres de l’Algo.
   - Recherche automatique de la meilleure fréquence à mesurer par l’Algo Goertzel, qui change d’un opérateur à l’autre.
   - Réglage automatique du niveau d’entrée grâce au potentiomètre numérique MCP41010 (10k, pilotable depuis le bus SPI)
     Le volume varie beaucoup d’un opérateur à l’autre, F5NWY arrivant S9+40 chez moi 😊

 25/12/2023 : Modifications V1.2 ==> V1.3a : (Publiée sur GitHub pour F8CND)
   - Intégration de l'Algo de F5BU basé sur la mesure des temps de dot / dash / silences

 27/12/2023 : Modifications V1.2 ==> V1.3b :
   - remplacement compteur (cptFreqLow) par la mesure du temps (using startLowSignal)
   - Suppression du code F5BU

 27/12/2023 : Modifications V1.3b ==> V1.3c :
   - Modif Algo : On base tout sur la mesure de moyDot, puis calcul de moyDash et moySP, séparation . / - using discri
     Ca marche beaucoup moins bien ☹️
   - Ajout trace pour visualiser sur le TFT : les min/max H et L, et les moyennes (moyDot, moyDash, moySP, discri, bMoy et barGraph)

 28/12/2023 : Modifications V1.3c ==> V1.3d :
   - Retour à l'Algo de G6EJD utilisant hightimesavg comme discriminateur . / -
   - Utilisation de VSCode + PlatformIO (à la place de l'IDE Arduino) : Le programme MorseDecoderV1.3d.ino devient src/main.cpp
   - Republication sur GitHub (sans le code de F5BU, et au format PlatformIO)

 30/12/2023 : Modifications V1.3d ==> V2.0 :
   - Ajout génération DataSet pour alimenter un réseau de Neurones
   - Gestion vitesse ADC : div=1 au lieu de 2 mis par PlatformIO

 03/01/2024 : Modifications V2.0 ==> V2.0a :
   - Connexion à CWDecoder-UI (programme Windows de réception décodage Morse et Play du son des lettre décodées)
   - On ne force plus un CR tous les 100 caractères (comme quand c'est l'IDE Arduino qui écoute)
   - On ajuste automatiquement nbSamples en fonction du WPM (Mesuré OK: 110samples pour 15WPM, 70samples pour 33WPM )

 =====================================================================================
 Morse Code Decoder using an OLED and basic microphone

 The MIT License (MIT) Copyright (c) 2017 by David Bird. 
 ### The formulation and calculation method of an IAQ - Internal Air Quality index ###
 ### The provision of a general purpose webserver ###
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files 
 (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, 
 publish, distribute, but not to use it commercially for profit making or to sub-license and/or to sell copies of the Software or to 
 permit persons to whom the Software is furnished to do so, subject to the following conditions:  
   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. 
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE 
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 See more at http://dsbird.org.uk 
 
 CW Decoder by Hjalmar Skovholm Hansen OZ1JHM  VER 1.01
 Feel free to change, copy or what ever you like but respect
 that license is http://www.gnu.org/copyleft/gpl.html
 Read more here http://en.wikipedia.org/wiki/Goertzel_algorithm 
 Adapted for the ESP32/ESP8266 by G6EJD  
*********************************************************************************************************/
#include "esp32/clk.h"
#include "driver/adc.h"

// F4LAA : Using TFT4 SPI display
#include "SPI.h"
#include "TFT_eSPI.h"

TFT_eSPI tft = TFT_eSPI();  

void tftDrawString(int x, int y, String s, bool disp = true)
{
  if (disp)
  {
    tft.setCursor(x, y);
    tft.println(s);
  }
}

// SPI Potentiometre
const int slaveSelectPin = 22; // CS 

#define POTMIDVALUE 128
uint8_t potVal = POTMIDVALUE;  // Middle value
uint8_t potCmd = 0x11; // =0b00010001 so set PotA value
void setVolume(uint8_t value) 
{ 
  digitalWrite(slaveSelectPin, LOW); 
  tft.spiwrite(potCmd);
  tft.spiwrite(255 - value);
  digitalWrite(slaveSelectPin, HIGH); 
  float pourcent = ((value / 255.00) * 100);
  tftDrawString(396, 280, String(pourcent, 0) + "%  ");
  potVal = value;
} 

// Variables G6EJD using Goertzel algorithm
float magnitude           = 0;;
int   magnitudelimit      = 100;
int   magnitudelimit_low  = 100;
int   realstate           = LOW;
int   realstatebefore     = LOW;
int   filteredstate       = LOW;
int   filteredstatebefore = LOW;

// Noise Blanker time which shall be computed so this is initial 
int nbTime = 6;  // ms noise blanker
int spaceDetector = 5;
int magReactivity = 6;

int starttimehigh;
int highduration;
int lasthighduration;
int starttimelow;
int lowduration;
int laststarttime = 0;
float hightimesavg = 0; // Séparation dot / dash et SP

// Encodeur rotatif GND, VCC, SW, DT (B), CLK (A)
// (A) CLK pin GPIO8 , (B) DT pin GPIO7, SW pin GPIO6 
#include <Rotary.h>
#define rotEncA 25
#define rotEncB 26
#define rotEncSW 27
Rotary rot= Rotary(rotEncA, rotEncB);
int rotCounter = 0;
int rotAState;
int rotALastState;
int rotSWState = 0;
int rotSWLastState = 0;
// EndOf Rotary variables definition

// Gestion des temps
// Stockage des temps : High & Silent pour chaque caractère décodé
#define MAXTIMES 11
int iTimes;
int dTimes[MAXTIMES];
int dTimes2[MAXTIMES];

void clearTimes(bool clone)
{
  if(clone)
    for (int i=0; i<MAXTIMES; i++)
    {
      dTimes2[i] = dTimes[i]; // Clone it
      dTimes[i] = 0;
    }
  else
    for (int i=0; i<MAXTIMES; i++)
    {
      dTimes[i] = 0;
      dTimes2[i] = 0;
    }

  iTimes = -1;
}
void addTime(int t)
{
  if (iTimes < MAXTIMES)
  {
    iTimes++;
    dTimes[iTimes] = t;
  }
}
void printTimes(char c)
{
  Serial.print(String(c));
  for (int i=0; i<MAXTIMES; i++)
  {
    Serial.print(";"); 
    Serial.print(dTimes2[i]); // Use cloned
  }
  Serial.println();
}

#define bufSize 8
int bufLen;
int sBufLen;
int startNoChange = 0;
char CodeBuffer[bufSize]; // 6 . ou - + 1 en trop (avant sécurité) + \0
char CodeBuffer2[bufSize]; // 6 . ou - + 1 en trop (avant sécurité) + \0
#define nbChars 33
char DisplayLine[nbChars];
int iRow = 0;
int iCar = 0;
int  stop = LOW;
int  wpm;
int  sWpm;

void clearDisplay()
{
  iRow = 0;
  tft.fillRect(0, 60, 480, 220, TFT_BLACK); // Clear display area
}

void clearDisplayLine()
{
  for (int i = 0; i < nbChars; i++) DisplayLine[i] = ' ';
}

void clearCodeBuffer(bool clone)
{
  clearTimes(clone);
  if (clone)
    strcpy(CodeBuffer2, CodeBuffer);
  CodeBuffer[0] = '\0';
  bufLen = 0;
  // for (int i = 1; i < bufSize; i++) CodeBuffer[i] = ' ';
};

// ADC speed problem 
// 11496 when the following code is not compiled (with ADCGives11496SampBySec defined)
// 9000 samp/s only when the code is compiled with ADCGives9000SampBySec defined)
// Response from platformIO team (05/01/2023 21:35)
// 9000 becomes 10086 when i add 
//   board_build.f_flash = 80000000L
// in platformio.ini file .... 
// with this change in platformIO.ini, div:1 is show at boot time :)
//
// But the ADC speed still decrease a lot (10086 instead of 11496)

#define ADCGives11496SampBySec
//#define ADCGives9000SampBySec
int cptNoChange = 0;
void clearIfNotChanged()
{
  // Clear buffer when no changes
#ifdef ADCGives11496SampBySec
  // Do not compile the code inside this function
#endif

#ifdef ADCGives9000SampBySec
  if ( (bufLen > 0) && (bufLen == sBufLen) )
  {
    cptNoChange++;
    if (cptNoChange > 500)
    {
      cptNoChange = 0;
      clearCodeBuffer(false);
    }
    if (startNoChange == 0)
      startNoChange = millis();
    else if (millis() - startNoChange > 3000) 
    {
      // Trop long sans changement de CodeBuffer
      startNoChange = 0;
      clearCodeBuffer(false);
    }
  }
  sBufLen = bufLen;
#endif
}

bool display = true;
int cptCharPrinted = 0;
bool CRRequested = false;
bool graph = false;   // To draw magnitude curve
bool dataSet = false; // To generate DataSet for Neural Network
#define MAXLINES 9
void AddCharacter(char newchar)
{
  if (CRRequested && (newchar != ' ')) 
  {
    CRRequested = false;
    cptCharPrinted = 0;
    // if (!graph && !dataSet)
    //   Serial.println(); // Inutile si avec CWDecoder-UI
  }

  iCar++;
  if (iCar == nbChars)
  {
    iCar = 0;
    int posRow = 60 + (iRow * 20);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tftDrawString(0, posRow, DisplayLine, display); // Affiche aussi CodeBuffer (qui suit DisplayLine en mémmoire et contient le \0)
    tft.fillRect(394, posRow, 72, 20, TFT_BLACK); // Clear CodeBuffer
    clearDisplayLine();
    iRow++;
    if (iRow > MAXLINES) 
      iRow = 0;
  }
  else
  {
    // Shift chars to get place for the new char
    for (int i = 0; i < nbChars; i++) 
      DisplayLine[i] = DisplayLine[i+1];
  }
  DisplayLine[nbChars - 1] = newchar;
}

char lastChar = '{';
char curChar = '{';
void CodeToChar() { // translate cw code to ascii character//
  clearCodeBuffer(true);
  char decodedChar = '{';
  if (strcmp(CodeBuffer2,".-") == 0)      decodedChar = char('a');
  if (strcmp(CodeBuffer2,"-...") == 0)    decodedChar = char('b');
  if (strcmp(CodeBuffer2,"-.-.") == 0)    decodedChar = char('c');
  if (strcmp(CodeBuffer2,"-..") == 0)     decodedChar = char('d'); 
  if (strcmp(CodeBuffer2,".") == 0)       decodedChar = char('e'); 
  if (strcmp(CodeBuffer2,"..-.") == 0)    decodedChar = char('f'); 
  if (strcmp(CodeBuffer2,"--.") == 0)     decodedChar = char('g'); 
  if (strcmp(CodeBuffer2,"....") == 0)    decodedChar = char('h'); 
  if (strcmp(CodeBuffer2,"..") == 0)      decodedChar = char('i');
  if (strcmp(CodeBuffer2,".---") == 0)    decodedChar = char('j');
  if (strcmp(CodeBuffer2,"-.-") == 0)     decodedChar = char('k'); 
  if (strcmp(CodeBuffer2,".-..") == 0)    decodedChar = char('l'); 
  if (strcmp(CodeBuffer2,"--") == 0)      decodedChar = char('m'); 
  if (strcmp(CodeBuffer2,"-.") == 0)      decodedChar = char('n'); 
  if (strcmp(CodeBuffer2,"---") == 0)     decodedChar = char('o'); 
  if (strcmp(CodeBuffer2,".--.") == 0)    decodedChar = char('p'); 
  if (strcmp(CodeBuffer2,"--.-") == 0)    decodedChar = char('q'); 
  if (strcmp(CodeBuffer2,".-.") == 0)     decodedChar = char('r'); 
  if (strcmp(CodeBuffer2,"...") == 0)     decodedChar = char('s'); 
  if (strcmp(CodeBuffer2,"-") == 0)       decodedChar = char('t'); 
  if (strcmp(CodeBuffer2,"..-") == 0)     decodedChar = char('u'); 
  if (strcmp(CodeBuffer2,"...-") == 0)    decodedChar = char('v'); 
  if (strcmp(CodeBuffer2,".--") == 0)     decodedChar = char('w'); 
  if (strcmp(CodeBuffer2,"-..-") == 0)    decodedChar = char('x'); 
  if (strcmp(CodeBuffer2,"-.--") == 0)    decodedChar = char('y'); 
  if (strcmp(CodeBuffer2,"--..") == 0)    decodedChar = char('z'); 
  
  if (strcmp(CodeBuffer2,".----") == 0)   decodedChar = char('1'); 
  if (strcmp(CodeBuffer2,"..---") == 0)   decodedChar = char('2'); 
  if (strcmp(CodeBuffer2,"...--") == 0)   decodedChar = char('3'); 
  if (strcmp(CodeBuffer2,"....-") == 0)   decodedChar = char('4'); 
  if (strcmp(CodeBuffer2,".....") == 0)   decodedChar = char('5'); 
  if (strcmp(CodeBuffer2,"-....") == 0)   decodedChar = char('6'); 
  if (strcmp(CodeBuffer2,"--...") == 0)   decodedChar = char('7'); 
  if (strcmp(CodeBuffer2,"---..") == 0)   decodedChar = char('8'); 
  if (strcmp(CodeBuffer2,"----.") == 0)   decodedChar = char('9'); 
  if (strcmp(CodeBuffer2,"-----") == 0)   decodedChar = char('0'); 

  if (strcmp(CodeBuffer2,"..--..") == 0)  decodedChar = char('?'); 
  if (strcmp(CodeBuffer2,".-.-.-") == 0)  decodedChar = char('.'); 
  if (strcmp(CodeBuffer2,"--..--") == 0)  decodedChar = char(','); 
  if (strcmp(CodeBuffer2,"-.-.--") == 0)  decodedChar = char('!'); 
  if (strcmp(CodeBuffer2,".--.-.") == 0)  decodedChar = char('@'); 
  if (strcmp(CodeBuffer2,"---...") == 0)  decodedChar = char(':'); 
  if (strcmp(CodeBuffer2,"-....-") == 0)  decodedChar = char('-'); 
  if (strcmp(CodeBuffer2,"-..-.") == 0)   decodedChar = char('/'); 

  if (strcmp(CodeBuffer2,"-.--.") == 0)   decodedChar = char('('); 
  if (strcmp(CodeBuffer2,"-.--.-") == 0)  decodedChar = char(')'); 
  if (strcmp(CodeBuffer2,".-...") == 0)   decodedChar = char('_'); 
  if (strcmp(CodeBuffer2,"...-..-") == 0) decodedChar = char('$'); 
  if (strcmp(CodeBuffer2,"...-.-") == 0)  decodedChar = char('>'); 
  if (strcmp(CodeBuffer2,".-.-.") == 0)   decodedChar = char('<'); 
  if (strcmp(CodeBuffer2,"...-.") == 0)   decodedChar = char('~'); 
  if (strcmp(CodeBuffer2,".-.-") == 0)    decodedChar = char('a'); // a umlaut
  if (strcmp(CodeBuffer2,"---.") == 0)    decodedChar = char('o'); // o accent
  if (strcmp(CodeBuffer2,".--.-") == 0)   decodedChar = char('a'); // a accent

  if (decodedChar != '{') {
    AddCharacter(decodedChar);
    if (!graph && !dataSet)
    {
      lastChar = curChar;
      curChar = decodedChar;
      cptCharPrinted++;
      if (cptCharPrinted > 100)
        CRRequested = true;
      Serial.print(decodedChar);
    }
    if (dataSet)
      printTimes(decodedChar);
  }
}

float goertzelCoeff; // For Goertzel algorithm
float Q1 = 0;
float Q2 = 0;

#define NBSAMPLEMIN 30
#define NBSAMPLEMAX 250
int testData[NBSAMPLEMAX];
int nbSamples = 100;
int newNbSamples = 100;
int sNewNbSamples = 100;

// you can set the tuning tone to 496, 558, 744 or 992
int iFreq;
int iFreqMax = 8; // = NbFreq - 1
int freqs[] = { 496, // for MorseSample-15WPM.wav file
                558, 
                610, // (CW IC-7300)
                640, // for Sergeï beacon on 28.222.800 Hz (CW IC-7300)
                677, // Balise HB9F (CW IC-7000)
                744, 
                992,
                1040, // for QSO PiouPiou CW on SSB (IC-7000)
                1136  // for QSO PiouPiou CW on SSB (IC-7000)
              };
bool autoTune = false;
bool sAutoTune = false;
int sensFreq = 1;
float sampling_freq = 0;
float target_freq = 0;
void setFreq(int freq)
{
  target_freq = freqs[freq];

  int k = (int) (0.5 + ((nbSamples * target_freq) / sampling_freq));
  float omega = (2.0 * PI * k) / nbSamples;
  goertzelCoeff = 2.0 * cos(omega);

  tft.fillRect(60, 20, 48, 20, TFT_BLACK);
  tftDrawString(60, 20, String(target_freq, 0));
}

float bw;
void setBandWidth(int nbsampl)
{
  bw = sampling_freq / nbsampl;
  tft.fillRect(180, 20, 36, 20, TFT_BLACK);
  tftDrawString(180, 20, String(bw, 0));
}

bool trace = false;
int idxCde= 0;
int idxCdeMax = 9;
char cdes[] = { 'F',  // sampling_freq
                'A',  // AutoTuneFreq
                'V',  // Volume
                'G',  // graph
                'D',  // display
                'T',  // trace
                'I',  // Generate DataSet fo Neural Network training
                'S',  // nbSamples
                'N',  // nbTime filter
                'R',  // magReactivity
                'B'   // Space detector
              }; 

void showCde(int cde)
{
  String cdeText = "";
  switch(cdes[cde])
  {
    case 'F':
      cdeText = "Freq";
      break;
    case 'A':
      if (autoTune)
        cdeText = "AutoTune ON";
      else
        cdeText = "AutoTune OFF";
      break;
    case 'V':
      cdeText = "Volume=" + String(potVal);
      break;
    case 'S':
      cdeText = "NbSample=" + String(nbSamples);
      break;
    case 'N':
      cdeText = "Filtre=" + String(nbTime);
      break;
    case 'R':
      cdeText = "MagReact=" + String(magReactivity);
      break;
    case 'B':
      cdeText = "DetectBL=" + String(spaceDetector);
      break;
    case 'G':
      if (graph)
        cdeText = "Graph ON";
      else
        cdeText = "Graph OFF";
      break;
    case 'D':
      if (display)
        cdeText = "Display ON";
      else
        cdeText = "Display OFF";
      break;
    case 'T':
      if (trace)
        cdeText = "Trace ON";
      else
        cdeText = "Trace OFF";
      break;
    case 'I':
      if (dataSet)
        cdeText = "DataSet ON";
      else
        cdeText = "DataSet OFF";
      break;
  }
  tft.fillRect(60, 300, 152, 20, TFT_BLACK);
  tftDrawString(60, 300, cdeText);
}

int cptLoop = 0;
void manageRotaryButton()
{
  // Manage Commands Rotary button
  // Rotary Encoder
  unsigned char dRot = rot.process();    
  if (dRot)
  {
    cptLoop = 0; // To show new acquired and loop time
    if (dRot != DIR_CW)
    {
      switch(cdes[idxCde])
      {
        case 'F':
          iFreq++;
          if (iFreq > iFreqMax)
            iFreq = iFreqMax;
          setFreq(iFreq);
          break;
        case 'A':
          autoTune = !autoTune;
          break;
        case 'V':
          potVal++;
          setVolume(potVal); 
          break;
        case 'S':
          nbSamples += 5;
          if (nbSamples > NBSAMPLEMAX)
            nbSamples = NBSAMPLEMAX;
          setBandWidth(nbSamples);
          break;
        case 'N':
          nbTime++;
          if (nbTime > 10)
            nbTime = 10;
          break;
        case 'R':
          magReactivity++;
          if (magReactivity > 10)
            magReactivity = 10;
          break;
        case 'B':
          spaceDetector++;
          if (spaceDetector > 10)
            spaceDetector = 10;
          break;
        case 'G':
          graph = !graph;
          break;
        case 'D':
          display = !display;
          if (!display)
            clearDisplay();
          break;
        case 'T':
          trace = !trace;
          if (!trace)
          {
            // Clear trace
            tft.fillRect(0, 220, 480, 60, TFT_BLACK);
          }
          break;
        case 'I':
          dataSet = !dataSet;
          if (dataSet)
          {
            sAutoTune = autoTune;
            autoTune = false;
          }
          else
            autoTune = sAutoTune;
          break;
      }        
    }
    else
    {
      switch(cdes[idxCde])
      {
        case 'F':
          iFreq--;
          if (iFreq < 0)
            iFreq = 0;
          setFreq(iFreq);
          break;
        case 'A':
          autoTune = !autoTune;
          break;
        case 'V':
          potVal--;
          setVolume(potVal); 
          break;
        case 'S':
          nbSamples -= 5;
          if (nbSamples < NBSAMPLEMIN)
            nbSamples = NBSAMPLEMIN;
          setBandWidth(nbSamples);
          break;
        case 'N':
          nbTime--;
          if (nbTime < 0)
            nbTime = 0;
          break;
        case 'R':
          magReactivity--;
          if (magReactivity < 1)
            magReactivity = 1;
          break;
        case 'B':
          spaceDetector--;
          if (spaceDetector < 0)
            spaceDetector = 0;
          break;
        case 'G':
          graph = !graph;
          break;
        case 'D':
          display = !display;
          if (!display)
            clearDisplay();
          break;
        case 'T':
          trace = !trace;
          if (!trace)
          {
            // Clear trace
            tft.fillRect(0, 220, 480, 60, TFT_BLACK);
          }
          break;
        case 'I':
          dataSet = !dataSet;
          if (dataSet)
          {
            sAutoTune = autoTune;
            autoTune = false;
          }
          else
            autoTune = sAutoTune;
          break;
      }
    }
    showCde(idxCde);
  }

  // Manage Rotary SW button
  rotSWState = digitalRead(rotEncSW);
  if (rotSWState != rotSWLastState)
  {
    if (rotSWState == LOW) // SW pressed
    {
      idxCde++;
      if (idxCde == idxCdeMax)
        idxCde = 0;
      showCde(idxCde);
    }        
    cptLoop = 0;
  }
  rotSWLastState = rotSWState;
}

void setup() {
  Serial.begin(115200);
  delay(1200); // 1200 mini to wait Serial is initialized...

  // TFT 4" SPI Init
  // Max SPI_FREQUENCY for this tft is 80000000 (80MHz) which is also the Max SPI speed for ESP32
  // It is only 10 MHz for SPI Potientiometer according to MCP41010 Datasheet, 
  // but it works well at 40MHz !!! (so, reduced to 40000000 in user_Setup.h of TFT Library (E:\Users\syst4\Documents\Arduino\libraries\TFT_eSPI-master)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_ORANGE);
  uint32_t cpu_freq = esp_clk_cpu_freq();
  tftDrawString(0, 5, "CW Decoder V2.0a (03/01/2024) by F4LAA (PlatformIO)             CpuFreq: " + String(cpu_freq / 1000000) + "MHz", true);
  tft.setTextSize(2);

  // Rotary encoder
  /* */
  rot.begin(true);
  rotALastState = digitalRead(rotEncA);
  pinMode (rotEncSW,INPUT_PULLUP);
  //rotSWState = digitalRead(rotEncSW);
  /* */

  // Gestion des ADC
  // Problème avec PlatformIO qui utilise un div 2 pour la clock de l'ADC
  // Au boot, on a : 
  //   mode:DIO, clock div:2
  // Alors que si compilé avec ArduinoIDE on a :
  //   mode:DIO, clock div:1

  // ADC speed problem
  //esp_err_t errCode = adc_set_clk_div(1); // Using PlatformIO, default=2 !!!!!! We need 1 (i.e no dividor) to get over 10k Samples/s
  //Serial.println("adc_set_clk_div(): ErrCode=" + String(errCode));
  // 05/01/2024 19:53 : 
  // After creating the following topic on PlatformIO
  //     https://community.platformio.org/t/platformio-esp32-adc-use-a-div-2-clock-while-arduino-ide-use-div-1-so-adc-becomes-too-slow/ 37645
  //     I commented the 2 lines above, to verify that samp/s fall to 7500, 7600 as i said in my topic
  //     and I now get 11496 samp/s with PlatformIO (while I only had 11246 samp/s on ArduinoIDE)
  //     and this always with a Div:2 when starting the ESP32 ???
  //   This is just incomprehensible!!!!!!!!!!!!!!!!!!!!!!!!!!!  
  // Now, i leave those 2 lines commented, 
  // and i try to see what happen to ADC speed when compiling ot not the function clearIfNotChanged()

  // Measure sampling_freq
  int tStartLoop = millis();
  int cpt = 0;
  while ( (millis() - tStartLoop) < 4000) { testData[0] = analogRead(A0); cpt++;}
  sampling_freq = cpt / 4;  // Measured at Startup on NodeMCU-32S  
  //Serial.println("sampling_freq=" + String(sampling_freq)); // 11496 when this line is commented !!!! and 10114 when this line is uncommented

  // Templates
  tft.setTextColor(TFT_SKYBLUE);
  tftDrawString(0, 20, "Freq=    Hz BW=   Hz WPM=   MAG=");
  tftDrawString(0, 280, "Acq=   ms", true);  
  tftDrawString(116, 280, "Loop=   ms", true);  
  tftDrawString(0, 300, "Cde:");
  tftDrawString(312, 280, "Volume:");
  tftDrawString(312, 300, "SmplFreq=", true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // BGColor need to be set to TFT_BLACK to prevent overwriting !!!
  tftDrawString(420, 300, String(sampling_freq, 0), true);
  //EndOfTemplates
  
  setBandWidth(nbSamples);

  //////////////////////////////////// The basic goertzel calculation //////////////////////////////////////
  // you can set the tuning tone to 496, 558, 744 or 992
  // The number of samples determines bandwidth
  iFreq = 3; // = 640Hz i.e. la frequence CW de l'IC-7300 
  setFreq(iFreq); 

  idxCde = 0;
  showCde(idxCde);

  clearDisplayLine(); // CodeBuffer suit en mémoire. C'est lui qui contient le \0 final
  clearCodeBuffer(true);

  // SPI Potentiometre (uses SPI instance defined in TFT library)
  pinMode (slaveSelectPin, OUTPUT); 
  setVolume(potVal); // Valeur mediane

  /* Debug SPI Potentiometer *
  int potSens = 1;
deb:
  potVal += potSens;
  if (potVal == 254)
   potSens = -1;
  if (potVal == 0)
   potSens = 1;
  setVolume(potVal);
  delay(500);
  goto deb;
  /* */
}

void changeVolume(int delta)
{
  int cValue = potVal + delta;
  if (cValue > 255)
    cValue = 255;
  if (cValue < 0)
    cValue = 0;
  setVolume(cValue);
}

long startLowSignal = 0;
long startLowSound = 0;
bool bScan = false;
void stopScan()
{
  startLowSignal = 0;
  bScan = false;  
}

int vMin = 32000;
int vMax = 0;
int tStartLoop;
int adcMidpoint = 1940; // Measured on NodeMCU32 with 3.3v divisor
float vMoy = adcMidpoint;
#define MAXMOY 20
int cptMoy = 0;
float bMoy = 0;
int dispMoy = 0;
int sBMoy = 0;
bool moyChanged = false;
bool moyComputed = false;
bool silentDuringSound = false;
int silent = 5; // barGraph silent level

void loop() {
  cptLoop++;
  if(cptLoop == 1)
  {
    tStartLoop = millis();
    //esp_err_t errCode = adc_set_clk_div(1); // Using PlatformIO, default=2 !!!!!! We need 1 (i.e no dividor) ti get over 10k Samples/s
  }

Acq:
  /* *
  // Measure sampling_freq
  int cpt0 = 0;
  tStartLoop = millis();
  while ( (millis() - tStartLoop) < 1000) { testData[0] = analogRead(A0); cpt0++;}
  if (cpt0 < vMin) vMin = cpt0;
  if (cpt0 > vMax) vMax = cpt0;
  Serial.println(String(vMin) + " " + String(cpt0) + " " + String(vMax));
  // Measured at : 11249 on NodeMCU-32S
  goto Acq;
  /* */

  // Measure adcMidpoint
  /* *
  //for (int index = 0; index < nbSamples; index++)
  int index = 0;
  {
    testData[index] = analogRead(A0);
    vMoy = ( (vMoy * 31) + testData[index] ) / 32;
    if (testData[index] < vMin) vMin = testData[index];
    if (testData[index] > vMax) vMax = testData[index];
    Serial.println(String(vMin) + " " + String(testData[index]) + " " + String(vMoy) + " " + String(vMax));
    // Measured at : 1950 on NodeMCU-32S avec diviseur du 3.3V
  }
  goto Acq;
  /* */

  // Acquisition
  for (int i = 0; i < nbSamples; i++) 
  {
    testData[i] = analogRead(A0);
  }

  if (cptLoop == 1)
  {
    int acqTime = millis() - tStartLoop;
    tftDrawString(48, 280, String(acqTime) + " ", true);  
  }

  // Compute magniture using Goertzel algorithm
  Q2 = 0;
  Q1 = 0;
  for (int index = 0; index < nbSamples; index++) {
    int curValue = testData[index] - adcMidpoint;
    float Q0 = (float)curValue + (goertzelCoeff * Q1) - Q2;
    Q2 = Q1;
    Q1 = Q0;
  }
  magnitude = sqrt( (Q1 * Q1) + (Q2 * Q2) - Q1 * Q2 * goertzelCoeff);

  // Adjust magnitudelimit
  if (magnitude > magnitudelimit_low) { magnitudelimit = (magnitudelimit + ((magnitude - magnitudelimit) / magReactivity)); } /// moving average filter
  if (magnitudelimit < magnitudelimit_low) magnitudelimit = magnitudelimit_low;

  // Now check the magnitude //
  if (magnitude > magnitudelimit * 0.3) // just to have some space up
    realstate = HIGH;
  else
    realstate = LOW;

  // Clean up the state with a noise blanker //  
  if (realstate != realstatebefore) 
  {
    laststarttime = millis();
  }
  if ((millis() - laststarttime) > nbTime) 
  {
    if (realstate != filteredstate) 
    {
      filteredstate = realstate;
    }
  }

  if (filteredstate != filteredstatebefore) 
  {
    if (filteredstate == HIGH) 
    {
      // front montant
      starttimehigh = millis();
      lowduration = (starttimehigh - starttimelow);
    }

    if (filteredstate == LOW) 
    {
      // front descendant
      starttimelow = millis();
      highduration = (starttimelow - starttimehigh);

      // Strange cumputation of hightimesavg (very low compared to average of highduration )
      if ( (highduration < (2 * hightimesavg)) || (hightimesavg == 0) ) 
      {
        hightimesavg = (highduration + hightimesavg + hightimesavg) / 3; // now we know avg dit time ( rolling 3 avg)
      }
      if (highduration > (5 * hightimesavg) ) 
      {
        hightimesavg = highduration + hightimesavg;   // if speed decrease fast ..
      }
    }
  }

  if (!bScan) // Not in search frequency mode
  {
    // Now check the baud rate based on dit or dah duration either 1, 3 or 7 pauses
    if (filteredstate != filteredstatebefore) {
      stop = LOW;
      if (filteredstate == LOW) { // we did end on a HIGH
        if (highduration < (hightimesavg * 2) && highduration > (hightimesavg * 0.6)) { /// 0.6 filter out false dits
          strcat(CodeBuffer, ".");
          addTime(highduration); // Dot duration
          bufLen++;
          //Serial.print(".");

          // if ( (highduration > 10)  // Ignore too short highduration caused by noise
          //      && 
          //      (highduration < 200) // Ignore too long highduration caused by silent
          //    )
          // {
          //   // Compute WPM based on Dot
          //   wpm = (wpm + (1200 / highduration )) / 2; //// the most precise we can do ;o)

          //   // Now, adjust NBSAMPLES according to WPM
          //   newNbSamples = map(wpm, 15, 33, 110, 70);  // Mesuré OK: 110samples pour 15WPM, 70samples pour 33WPM       
          //   if (abs(newNbSamples - sNewNbSamples) > 2) 
          //   {
          //     nbSamples = newNbSamples;
          //     setBandWidth(nbSamples);
          //   }
          //   sNewNbSamples = newNbSamples;
          // }
        }
        
        if (highduration > (hightimesavg * 2) && highduration < (hightimesavg * 6)) {
          strcat(CodeBuffer, "-");
          addTime(highduration); // Dash duration
          bufLen++;
          //Serial.print("-");

          if ( (highduration > 66) // Ignore too short highduration caused by silent
               && 
               (highduration < 500) // Ignore too long highduration caused by silent
             )
          {
            // Compute WPM based on Dash
            wpm = (wpm + (1200 / ((highduration) / 3))) / 2; //// the most precise we can do ;o)

            // Now, adjust NBSAMPLES according to WPM
            newNbSamples = map(wpm, 15, 33, 110, 70);  // Mesuré OK: 110samples pour 15WPM, 70samples pour 33WPM       
            if (abs(newNbSamples - sNewNbSamples) > 2) 
            {
              nbSamples = newNbSamples;
              setBandWidth(nbSamples);
            }
            sNewNbSamples = newNbSamples;
          }
        }
      }

      if (filteredstate == HIGH) { // we did end a LOW
        float lacktime = 1;
        if (wpm > 25) lacktime = 1.0; ///  when high speeds we have to have a little more pause before new letter or new word
        if (wpm > 30) lacktime = 1.2;
        if (wpm > 35) lacktime = 1.5;

        bool storeTime = true;
        if (lowduration > (hightimesavg * (2 * lacktime)) && lowduration < hightimesavg * (5 * lacktime)) { // letter space
            storeTime = false;
            CodeToChar();
        }

        if (lowduration >= hightimesavg * (spaceDetector * lacktime)) { // word space
          storeTime = false;
          CodeToChar();        
          AddCharacter(' ');
          if (!graph && !dataSet)
          {
            Serial.print(" ");
            if ( (lastChar == 'b') and (curChar == 'k') ) // EOL
            {
              Serial.println("<===");
              CRRequested = false;
              cptCharPrinted = 0;
            }
          }
        }

        if (storeTime) 
          addTime(lowduration); // Silent inside char
      }
    } // filteredstate != filteredstatebefore

    if ((millis() - starttimelow) > (highduration * 6) && stop == LOW) {
      CodeToChar();
      stop = HIGH;
    }

    clearIfNotChanged();

    // Sécurité buffer overflow
    if (strlen(CodeBuffer) == bufSize - 1) {
      // On a reçu des . et -, mais pas de silence...
      clearCodeBuffer(false);
    }
  } // !bScan

  if (graph)
  {
    if (magnitude < vMin) vMin = magnitude;
    if (magnitude > vMax) vMax = magnitude;
    int drawFilteredState;
    if (filteredstate == HIGH)
      drawFilteredState = vMax + 1000;
    else
      drawFilteredState = vMin - 1000;
    Serial.println(String(magnitude) + " " + String(drawFilteredState) + " " + String(magnitudelimit));
  }

  if (!bScan)
  {
    // Update display
    // Decoded CW  
    int posRow = 60 + (iRow * 20);
    tft.fillRect(394, posRow, 72, 20, TFT_BLACK); // Clear CodeBuffer
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tftDrawString(0, posRow, DisplayLine, display); // Affiche aussi CodeBuffer (qui suit DisplayLine en mémmoire et contient le \0)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // WPM
    if (abs(sWpm - wpm) >= 5)
    {
      sWpm = wpm;
      tft.fillRect(302, 20, 24, 20, TFT_BLACK);
      tftDrawString(302, 20, String(wpm));
    }
  }

  // BarGraph Magnitude
  int barGraph = magnitude / 100;
  if (barGraph > 100)
    barGraph = 100;
  if (barGraph > silent) 
  {
    // Sound detected
    startLowSound = 0;
    silentDuringSound = false;
    stopScan();
    bMoy = ( ( (bMoy * cptMoy) + barGraph ) / (cptMoy + 1) );
    dispMoy = bMoy;
    if (dispMoy > 97)
      dispMoy = 97; // Pour ne as dépasserr les 480 pixels
    moyChanged = true; //(abs(sBMoy - bMoy) > 1);
    sBMoy = bMoy;
    cptMoy++;
    if (cptMoy > MAXMOY) 
      cptMoy = MAXMOY;
    moyComputed = (cptMoy == MAXMOY);
  }
  else
  {
    // Silence
    if (moyComputed)
      silentDuringSound = true;
    if (startLowSound == 0)
      startLowSound = millis();
    else if ((millis() - startLowSound) > 10000) // 10s with low sound
    {
      startLowSound = 0;
      silentDuringSound = false;
      moyComputed = false;
    }
  }
  
  if (trace)
  {
    // Affichage valeurs barGraph et bMoy
    tftDrawString(0, 260, "bMoy=" + String(bMoy) + "    barG=" + String(barGraph) + "   ");
  }

  if (moyChanged || bScan)
    tft.fillRect(387, 23, 93, 10, TFT_BLACK); // Clear BarGraph
  
  if (barGraph > 20)
  {
    // Sound detected
    if (bMoy > 75) 
    {
      // bMoy in [76..100]
      if (moyChanged)
        tft.fillRect(387, 23, dispMoy, 10, TFT_RED); // Draw BarGraph
      if (moyComputed)
        changeVolume(-20);
    }
    else if (bMoy > 50)
    {
      // bMoy in [51..75]
      if (moyChanged)
      {
        sBMoy = bMoy;
        tft.fillRect(387, 23, dispMoy, 10, TFT_ORANGE); // Draw BarGraph
      }
      if (moyComputed)
        changeVolume(-10);
    }
    else if (bMoy > 20)
    {
      // bMoy in [21..50]
      if (moyChanged)
      {
        tft.fillRect(387, 23, dispMoy, 10, TFT_GREEN); // Draw BarGraph
      }
    }
  }
  else
  { 
    // Low sound detected
    // barGraph in [silent..20]
    if (moyChanged || bScan)
      tft.fillRect(387, 23, barGraph, 10, TFT_LIGHTGREY); // Draw BarGraph

    // Something heard, but low : Increase volume
    if (!silentDuringSound)
    {
      // Increase volume
      if (barGraph > 20)
        // barGraph in [21..30]
        changeVolume(2);
      else
        // barGraph in [10..20]
        changeVolume(4);
    }

    if (barGraph < 10)
    {
      // barGraph in [silent..9]
      // Very weak signal heard : Try to find better frequency tune
      if (autoTune)
      {
        if (startLowSignal == 0)
          startLowSignal = millis();
        else if ((millis() - startLowSignal) > 5000) // 5s without audible signal
        {
          bScan = true;
          setVolume(POTMIDVALUE); // Middle value
          cptMoy = 0;
          moyComputed = false;
          starttimehigh = 0;
          starttimelow = 0;
          lowduration = 0;         
          highduration = 0;         

          // Search for a better iFreq
          iFreq += sensFreq;
          if (iFreq > iFreqMax)
            iFreq = iFreqMax;
          if (iFreq < 0)
            iFreq = 0;
          if (iFreq == iFreqMax)
            sensFreq = -1;
          if (iFreq == 0)
            sensFreq = 1;
          setFreq(iFreq);
        }
      }
    }
  }

  // the end of main loop clean up//
  realstatebefore     = realstate;
  lasthighduration    = highduration;
  filteredstatebefore = filteredstate;

  if (cptLoop == 1)
  {
    int loopTime = millis() - tStartLoop;
    tftDrawString(176, 280, String(loopTime) + " ", true);  
  }

  manageRotaryButton();

 // EndOfLoop
}