// This sketch sends sensors dta tp Cayenne with LPP format
// but it use advanced Ultra Low Power techniques
// for this it disable all unneeded peripherals during sleep mode, including
// USB Management, this mean you won't be able to upload anymore if the node is sleeping
// when wake up, Lora transmission is approx 3s (including receive windows) this means
// that you have 3 seconds windows to upload, so unless you're lucky, it's almost impossible
// to sync Arduino compilation and upload. But to avoid this, you can press the node button
// for more than 2s, then the led will yellow blink quickly for 60s, letting you time to upload 

#include <TheThingsNode.h>
#include <CayenneLPP.h>

// Set your AppEUI and AppKey
const char *appEui = "0000000000000000";
const char *appKey = "00000000000000000000000000000000";

// Replace REPLACE_ME with TTN_FP_EU868 or TTN_FP_US915
#define freqPlan REPLACE_ME

#define loraSerial Serial1
#define debugSerial Serial

TheThingsNetwork ttn(loraSerial, debugSerial, freqPlan);
TheThingsNode *node;
CayenneLPP lpp(51);

#define PORT_SETUP 1
#define PORT_INTERVAL 2
#define PORT_MOTION 3
#define PORT_BUTTON 4

// Interval between send in seconds, so 300s = 5min
#define CONFIG_INTERVAL ((uint32_t)300)

// Timeout after stop moving in seconds to resend start motion
#define MOVE_TIMEOUT ((uint32_t)30)

// Force use MOVE INTERVAL after x times start detected before end MOVE_TIMEOUT (always moving object).
#define MAX_NBSTART ((uint16_t)10)

// Timeout after MAX_NBSTOP stop detected before end MOVE_TIMEOUT, in seconds to resend start motion
#define MOVE_INTERVAL ((uint32_t)60)

bool move_start = false;
bool move_stop = false;
bool move_timeout = false;
bool use_move_interval = false;
uint16_t nbstart_detect = 0;

void sendData(uint8_t port=PORT_SETUP, uint32_t duration=0);

void setup()
{
  loraSerial.begin(57600);
  debugSerial.begin(9600);

  // Wait a maximum of 10s for Serial Monitor
  while (!debugSerial && millis() < 10000) {
    node->setColor(TTN_RED);
    delay(20);
    node->setColor(TTN_BLACK);
    delay(480);
  };

  // Config Node
  node = TheThingsNode::setup();
  node->configLight(true);
  node->configTemperature(false); // Just read temp, no alert monitoring (Low Power)

  // If you want alerts (200uA to 400uA more consumption)
  // node->configTemperature(true); // Alerts below 0C and over 30C and critical to 55C
  // node->configTemperature(true, R_DEGREES_0_0625, 15, 25, 50); // Alerts below 15C and over 25C and critical to 50C
  // node->onTemperatureAlert(yourAlertCallback); // Your alert callback

  node->onWake(wake);
  node->onInterval(interval);
  node->onSleep(sleep);
  node->onMotionStart(onMotionStart);
  node->onMotionStop(onMotionStop);
  node->onButtonRelease(onButtonRelease);

  // Test sensors 
  node->showStatus();

  debugSerial.println("-- TTN: STATUS");
  ttn.showStatus();

  // Each interval (with watchdog)
  //node->configInterval(true, CONFIG_INTERVAL*1000); 
  
  // Each interval (with Lora Module and Serial IRQ)
  // Take care this one need to be called after any
  // first call to ttn.* so object has been instancied
  // if not &ttn will be null and watchdog will wake us
  node->configInterval(&ttn, CONFIG_INTERVAL*1000); 

  // Magenta during join, if joined then green else red
  debugSerial.println(F("-- TTN: JOIN"));
  node->setColor(TTN_MAGENTA);
  if (ttn.join(appEui, appKey)) {
    node->setColor(TTN_GREEN);
  } else {
    node->setColor(TTN_RED);
  }

  debugSerial.println("-- SEND: SETUP");
  sendData(PORT_SETUP);

  // Enable sleep even connected with USB cable
  // node->configUSB(true);
}

void color_blink(ttn_color color, uint8_t count)
{
  for (uint8_t i=0 ; i<count ; i++) {
    node->setColor(color);
    delay(30);
    node->setColor(TTN_BLACK);
    delay(470);
  }
}

void loop()
{
  node->loop();
}

void interval(uint8_t wakeReason)
{
  if (wakeReason != 0)
  {
    debugSerial.print(F("-- SEND: INTERVAL 0x"));
    debugSerial.print(wakeReason, HEX);
    debugSerial.println(F("ms"));

    if (move_timeout || use_move_interval)
    {
      // interval set after stop mouvement so reset to detect it after
      color_blink(TTN_WHITE, 5);
      
      if (use_move_interval)
      {
        sendData(PORT_INTERVAL);
        nbstart_detect = 0;
        use_move_interval = false;
      }

      move_start = false;
      move_stop = false;
      move_timeout = false;
      // Back to TTN interval
      node->configInterval(&ttn, CONFIG_INTERVAL*1000);
    }
    else
      sendData(PORT_INTERVAL);
  }
}

void wake(uint8_t wakeReason)
{
  if (wakeReason != 0)
  {
    debugSerial.print(F("-- WAKE: 0x"));
    debugSerial.println(wakeReason, HEX);

    if (wakeReason & TTN_WAKE_WATCHDOG) {
      debugSerial.print(F(" Watchdog"));
      color_blink(TTN_YELLOW, 2);
    }
    else
  	  node->setColor(TTN_GREEN);
  }
}

void sleep()
{
  node->setColor(TTN_BLACK);
}

void onMotionStart()
{
  node->setColor(TTN_RED);

  // Send First Move
  // Then wait Stop Motion and End of Move Timeout To send next Motion receive
  // this avoid flooding network when the device is all time moving
  if (move_start || use_move_interval)
  {
    if (move_stop && !use_move_interval)
	  {
      nbstart_detect ++;
      debugSerial.print(F("-- DETECT: NB START BEFORE TIMEOUT : "));
      debugSerial.print(nbstart_detect);

      if (nbstart_detect > MAX_NBSTART)
      {
        debugSerial.print("-- MAX NB STOP, USE MOVE INTERVAL");
        // SET MOVE INTERVAL
        use_move_interval = true;
        node->configInterval(&ttn, MOVE_INTERVAL*1000);
      }
      else
      {
        move_stop = false;
        move_timeout = false;
  	    // Back to TTN interval with MOVE_INTERVAL
        node->configInterval(&ttn, MOVE_INTERVAL*1000);
      }
	  }
    else
      move_start = true;
  }
  else {
    nbstart_detect = 0;
    move_start = true;
    move_stop = false;
    debugSerial.print("-- DETECT: MOTION START");
    debugSerial.print("-- SEND: START MOTION");
    sendData(PORT_MOTION);
  }
}

void onMotionStop(unsigned long duration)
{
  if (move_start)
  {
    if (move_stop)
    {
      debugSerial.print("-- DETECT: STOP MOTION AFTER FIRST STOP ");
      move_start = false;
    }
    else
    {
      debugSerial.print("-- DETECT: FIRST STOP MOTION");

      node->setColor(TTN_CYAN);
      move_stop = true;
      move_timeout = true;
      // SET MOVE TIMEOUT
      node->configInterval(true, MOVE_TIMEOUT*1000);
     }
  }
  else
  {
    move_stop = false;
	  // Back to TTN interval
    move_timeout = false;
    // Back to TTN interval
    node->configInterval(&ttn, CONFIG_INTERVAL*1000);
  }
}

void onButtonRelease(unsigned long duration)
{
  uint32_t timepressed = (uint32_t) duration;

  debugSerial.print(F("-- SEND: BUTTON "));
  debugSerial.print(timepressed);
  debugSerial.println(F("ms"));

  // If button was pressed for more then 2 seconds
  if (timepressed > 2000) {
    // blink yellow led for 60 seconds
    // this will let us to upload new sketch if needed
    color_blink(TTN_YELLOW, 60);
  }

  // then send data
  sendData(PORT_BUTTON, timepressed);
}

void sendData(uint8_t port, uint32_t duration)
{
  uint16_t bat = node->getBattery();

  // when battery < 3.3V (regulator does not regule output, so 
  // we use another method to read VCC of the device and this will 
  // also avoid error if VCC <  ADC reference (2.52V)
  if (bat <= 3300) 
  {
    bat = node->getVCC();
  }

  // Wake Lora Module
  ttn.wake();

  printSensors();


  lpp.reset();
  lpp.addDigitalInput(1, node->isButtonPressed());
  lpp.addDigitalInput(2, node->isUSBConnected());
  lpp.addDigitalInput(3, move_start);
  lpp.addAnalogInput(4, bat/1000.0);
  lpp.addTemperature(5, node->getTemperatureAsFloat());
  lpp.addLuminosity(6, node->getLight());
  
  float x,y,z;
  node->getAcceleration(&x, &y, &z);
  lpp.addAccelerometer(7, x, y, z);

  // If button pressed, send press duration
  // please myDeviceCayenne add counter value type to
  // avoid us using analog values to send counters
  if (duration) 
  {
    debugSerial.print(F("Button pressed for "));
    debugSerial.print(duration);
    debugSerial.println(F("ms"));
    lpp.addAnalogInput(10, duration/1000.0);
  }
  
  node->setColor(TTN_BLUE);
  ttn.sendBytes(lpp.getBuffer(), lpp.getSize(), port);
}

void printSensors()
{
  debugSerial.println();
  debugSerial.println("SENSORS:");
  debugSerial.println("--------");
  
  debugSerial.print("LED Colour:\t");
  debugSerial.println(node->colorToString(node->getColor()));

  debugSerial.print("Temperature:\t");
  debugSerial.print(node->getTemperatureAsFloat());
  debugSerial.println("°C");

  debugSerial.print("Light Sensor:\t");
  debugSerial.print(node->getLight());
  debugSerial.println("lux");
  
  debugSerial.print("Moving now:\t");
  if(node->isMoving())
  {
    debugSerial.println("yes");
  }
  else
  {
    debugSerial.println("no");
  }
  
  debugSerial.print("Button pressed:\t");
  if(node->isButtonPressed())
  {
    debugSerial.println("yes");
  }
  else
  {
    debugSerial.println("no");
  }

  debugSerial.print("USB connected:\t");
  if(node->isUSBConnected())
  {
    debugSerial.println("yes");
  }
  else
  {
    debugSerial.println("no");
  }

  debugSerial.print("Battery:\t");
  debugSerial.print(node->getBattery());
  debugSerial.println("mV");

  float x,y,z;
  node->getAcceleration(&x, &y, &z);

  debugSerial.print("Acceleration:\tx=");
  debugSerial.print(x);
  debugSerial.print("g\n\t\ty=");
  debugSerial.print(y);
  debugSerial.print("g\n\t\tz=");
  debugSerial.print(z);
  debugSerial.println("g");

  debugSerial.println("--------");
  debugSerial.println();
  
}

