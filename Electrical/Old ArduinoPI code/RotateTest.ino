/*
 * This is a test for a function designed to rotate the motor x rotations
 * When rotate is called it turns on the motor in the direction specified and sets two global variables:
 *  startTick is the pulse number reported by enc.read() that the motors began moving at.
 *  stopTick is how many pulses need to pass before the motor is stopped.
 * 
 * It uses the main loop to determine when to stop the motor since I wanted the arduino free to do other stuff while the motor is running.
 *  I dont really like this solution but cant think of anything else besides using the interrupts from the encoder to check the stopping condition but I feel like that would use up too much of the arduinos processing time.
 *  If someone has a better solution that would be great
 *  
 *  This was tested on the larger motor. The smaller motor probably has a different number of pulses for each revolution but I cant test that cause I think my motors encoder is broken.
 *  
 *  setting the speed dosent currently work and I'm not sure why I'll fix that later.
 *  
 *  UPDATE: Added functionality to get I2C data in and control the motor via that
 *  
 */

//#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Wire.h>
#include <math.h>

#define encoderA 10
#define encoderB 6
#define motorIn1 4
#define motorIn2 5
//#define motorPWM 6

#define smallEncoder 3
//#define smallEncoderB 7
#define smallIn1 8
#define smallIn2 9
//#define smallPWM 6

#define limitSwitch 2

#define ADDRESS 4

//Amount of encoder pulses per revolution
//This number is experimentally found and probably not accurate but I cant find a specs sheet on the encoder for the big motor so this is what im working with
#define ticksPerRevolution 10200

Encoder* enc = new Encoder(encoderA, encoderB);
//Encoder* smallEnc = enc;// = new Encoder(smallEncoderA, smallEncoderB);
long smallEnc = 0;

unsigned int smallTicksPerRevolution = 1000;
bool moveDir = 0;

//startTick and stopTick are used to figure out when the motor started turning and when it should stop
long startTick = 0;
long stopTick = 0;

long smallStartTick = 0;
long smallStopTick = 0;

long requestQueue = -1;

//This is used to know what information the pi is requesting when it gets a request 
int8_t requestType = -1;

bool turnDirection;

bool responseRequested = false;
bool wheelDone = false;
bool turnDone = false;

const long driveMotorDoneCode = 500;
const long turnMotorDoneCode = 600;

uint8_t calibrationState = 0;
bool pressed = false;
unsigned long limitBuffer = 0;

//Function rotates the drive motor a specified number of revolutions
void rotate(float revolutions, bool direction = 0, float speed = 1.0){
  //analogWrite(motorPWM, (int)(speed*255));
  if(direction){
    digitalWrite(motorIn1, HIGH);
    digitalWrite(motorIn2, LOW);
  }else{
    digitalWrite(motorIn1, LOW);
    digitalWrite(motorIn2, HIGH);
  }

  //stopTick is how many ticks need to pass to stop the motor
  //startTick is the position the encoder was at when it started moving
  stopTick = revolutions*ticksPerRevolution;
  startTick = enc->read();
}

//Extremely similar to the rotate function except it takes in degrees instead of revolutions and calculates how many revolutions the motor needs to turn to get the wheel to turn that amount of degrees
void turnWheel(float deg, bool direction = 0){
  //analogWrite(smallPWM, 255);
  moveDir = direction;
  if(direction){
    digitalWrite(smallIn1, HIGH);
    digitalWrite(smallIn2, LOW);
  }else{
    digitalWrite(smallIn1, LOW);
    digitalWrite(smallIn2, HIGH);
  }

  smallStopTick = deg*smallTicksPerRevolution/360;
  smallStartTick = smallEnc;
}

void setup() {
  //Init pins
  pinMode(motorIn1, OUTPUT);
  pinMode(motorIn2, OUTPUT);
  //pinMode(motorPWM, OUTPUT);
  pinMode(smallIn1, OUTPUT);
  pinMode(smallIn2, OUTPUT);
  //pinMode(smallPWM, OUTPUT);

  pinMode(smallEncoder, INPUT);

  pinMode(limitSwitch, INPUT);

  

  //Join I2C with ADDRESS
  Wire.begin(ADDRESS);

  //Set the receive and request events
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  //Start serial bus
  Serial.begin(9600);

  //Set encoder interrupt
  attachInterrupt(digitalPinToInterrupt(smallEncoder), encInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(limitSwitch), limitSwitchInterrupt, RISING);
  
}

void loop() {
  // put your main code here, to run repeatedly:


  //Stop the drive motor if enough ticks have passed
  if(stopTick && abs(startTick - enc->read()) > abs(stopTick)){
    digitalWrite(motorIn1, LOW);
    digitalWrite(motorIn2, LOW);
    //analogWrite(motorPWM, 255);

    if(responseRequested){
      wheelDone = true;
      responseRequested = false;
    }

    stopTick = 0;
  }

  //Stop the small motor if enough ticks have passed
  if(smallStopTick && abs(smallStartTick - smallEnc) > abs(smallStopTick)){
    digitalWrite(smallIn1, LOW);
    digitalWrite(smallIn2, LOW);
    //analogWrite(smallPWM, 255);

    if(responseRequested){
      turnDone = true;
      responseRequested = false;
    }
    smallStopTick = 0;
  }
  
  if(pressed){
    pressed = false;

    if(millis() - limitBuffer < 200){
      limitBuffer = millis();
      return;
    }
    limitBuffer = millis();
    Serial.println("Pressed");

    if(!calibrationState) return;
  
    if(calibrationState == 1){
      smallEnc = 0;
      calibrationState = 2;
    }else if(calibrationState == 2){
      smallTicksPerRevolution = abs(smallEnc);
      smallEnc = 0;
      digitalWrite(smallIn1, LOW);
      digitalWrite(smallIn2, LOW);
      calibrationState = 0;
      if(responseRequested){
        turnDone = true;
        responseRequested = false;
      }
    }else if(calibrationState == 3){
      smallEnc = 0;
      digitalWrite(smallIn1, LOW);
      digitalWrite(smallIn2, LOW);
      calibrationState = 0;
      if(responseRequested){
        turnDone = true;
        responseRequested = false;
      }
    }
  }

  //Serial.println(smallEnc);

  //Serial.println(requestQueue);
}

//Made this a function because all of this was repeated in the turn wheel and drive wheel functions. Already made the comments and didnt feel like changing them or variable names so pretend this is in the Rotate Wheel function of the recieveEvent where its called
void getFloatAndDir(int numBytes, byte* input, float* outputFloat, bool* outputBool){
  //Set the default values for amount of revolutions and direction if not specified by input
  *outputFloat = 1;
  *outputBool = 0;

  //I2C cannot transmit floats/doubles (at least without some pre and post processing) so they have to be converted into an int and an exponent of 10
  //revolutionsBase is the int and revolutionsExponent is the exponent of 10, so the final float is revolutionsBase*10^revolutionsExponent
  //It would have been easier to just get a refrence to the float, cast int* to the pointer and derefrence it to get an integer and send that int over but python cant do that so we have to deal with this method.
  long revolutionsBase;
  int8_t revolutionsExponent;
  if(numBytes > 1){ //Make sure message included the amount of revolutions
    byte intIn[4] = {0, 0, 0, 0}; //An int is 4 bytes so make an array of those 4 bytes

    //The sender may have sent the base in more or less than 4 bytes so we transpose those bytes into the new array with max length of 4
    //The first byte specifies how many bytes the int is stored in. Anything beyond 4 bytes will be truncated but I doubt were ever gonna tell it to rotate 2147483.647 times
    uint8_t intInLength = input[0]; 
    for(uint8_t i = 0; i < min(4, intInLength); i++){
      intIn[i] = input[1 + i];
    }

    //Convert the byte array to an int
    revolutionsBase = *(long*)intIn;
    Serial.println(revolutionsBase);
    revolutionsExponent = 0;

    if(numBytes >= intInLength + 3){ //If enough arguments exist, the last one should be the direction and should be the last argument so set the direction to that
      *outputBool = (bool)input[intInLength + 2];
    }
    if(numBytes >= intInLength + 2){ //If enough arguments exist, this one should be the exponent for the revolutionsBase
      revolutionsExponent = input[intInLength + 1];
    }

    //Swap direction if the revolutions is negative
    if(revolutionsBase < 0) {
      *outputBool = !(*outputBool);
      revolutionsBase *= -1;
    }

    //Calculate the amount of revolutions as a float
    *outputFloat = (float)revolutionsBase*pow(10, revolutionsExponent);
  }
}

//Called whenever the Pi sends something over I2C
//numBytes is how many bytes were transmitted
void receiveEvent(int numBytes){
  //Create an array of all the bytes the Pi sent over
  //first is the first byte the Pi sent over, which is the command, and everything in the array is extra information needed
  byte input[numBytes - 1];
  byte first = Wire.read();

  if(first == 1){
    requestQueue = (long)(1000*enc->read()/ticksPerRevolution);
    return;
  }else if(first == 8){
    requestQueue = (long)(1000*smallEnc*360/smallTicksPerRevolution);
    return;
  }else if(first == 9){
    if(wheelDone){
      requestQueue = driveMotorDoneCode;
      wheelDone = false;
    }else if(turnDone){
      requestQueue = turnMotorDoneCode;
      turnDone = false;
    }else{
      requestQueue = -1;
      responseRequested = true;
    }
    return;
  }
  for(int i = 0; Wire.available(); i++){
    input[i] = Wire.read();
    Serial.print(input[i]);
    Serial.print(", ");
  }
  Serial.print('\n');

  //This is basically just a switch statement for each command the Pi could send over
  //I realize this should be a switch statement but for whatever reason doing it as one refused to work so its an if ... else if ... statement
  //Command number : command
  //  0 : Rotate drive wheel
  //  1 : Request drive motor position in revolutions
  //  2 : Turn drive motor on until stop command is sent
  //  3 : Turn drive motor off
  //  4 : Turn the wheel the specified amount of degrees
  if(first == 0){ //Rotate Wheel
    // Arguments should be in <Amount of bytes in base int, base int(should be 4 bytes), exponent, direction> format exponent and direction are optional and will be set to 0 if not set

    float revolutions;
    bool direction;
    getFloatAndDir(numBytes, input, &revolutions, &direction);

    Serial.println(revolutions);
    
    //Call the rotate function
    rotate(revolutions, direction);
  }else if(first == 2){ //On
    //Only argument for this is the direction (0 or 1) and is optional
    bool dir = 0;

    //If direction was specified set the direction to that
    if(numBytes > 1) dir = input[0];

    //Turn the motor on
    //analogWrite(motorPWM, 255);
    if(dir){
      digitalWrite(motorIn1, HIGH);
      digitalWrite(motorIn2, LOW);
    }else{
      digitalWrite(motorIn1, LOW);
      digitalWrite(motorIn2, HIGH);
    }
  }else if(first == 3){ //Off
    //Turn the drive motor off (no arguments needed)
    digitalWrite(motorIn1, LOW);
    digitalWrite(motorIn2, LOW);
  }else if(first == 4){//Turn wheel (Basically same thing as the rotate function but runs a different function at the end)
    // Arguments should be in <Amount of bytes in base int, base int(should be 4 bytes), exponent, direction> format exponent and direction are optional and will be set to 0 if not set

    float revolutions;
    bool direction;
    getFloatAndDir(numBytes, input, &revolutions, &direction);

    Serial.println(revolutions);
    //Call the rotate function
    turnWheel(revolutions, direction);
  }else if(first == 5){
    moveDir = true;
    digitalWrite(smallIn1, HIGH);
    digitalWrite(smallIn2, LOW);
    calibrationState = 1;
  }else if(first == 6){ //Return to position 0 without calibrating the encoder
    if(smallEnc <= 1 && smallEnc >= -1){
      return;
    }

    if((smallEnc%smallTicksPerRevolution < smallTicksPerRevolution/2) != (smallEnc < 0)){
      digitalWrite(smallIn1, LOW);
      digitalWrite(smallIn2, HIGH);
      moveDir = false;
    }else{
      digitalWrite(smallIn1, HIGH);
      digitalWrite(smallIn2, LOW);
      moveDir = true;
    }

    calibrationState = 3;
  }else if(first == 7){ //Rotate to a specified number of degrees

    float revolutions;
    bool direction;
    getFloatAndDir(numBytes, input, &revolutions, &direction);
    
    if(smallEnc < (2*(direction) - 1)*revolutions*smallTicksPerRevolution/360 == turnDirection){
      digitalWrite(smallIn1, LOW);
      digitalWrite(smallIn2, HIGH);
      moveDir = false;
    }else{
      digitalWrite(smallIn1, HIGH);
      digitalWrite(smallIn2, LOW);
      moveDir = true;
    }
    
    smallStartTick = smallEnc;
    smallStopTick = abs(smallEnc - (2*(direction) - 1)*revolutions*smallTicksPerRevolution/360);

    
  }else{//Function not found
    Serial.print("No function: ");
    Serial.println(first);
  }
}

//Called when the Pi requests data
//requestType should have been set in the recieveEvent function just before this is called
void requestEvent(){
  Wire.write((byte*)&requestQueue, sizeof(requestQueue));
  requestQueue = -1;
}

void encInterrupt(){
  if(moveDir) smallEnc++;
  else smallEnc--;
}

void limitSwitchInterrupt(){
  pressed = true;
  
}
