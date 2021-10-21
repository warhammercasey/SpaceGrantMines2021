import smbus, time, struct, threading

#This script contains the class Wheel which contains information and functions so that the wheels can be controlled at a high level.
#The constructor requires the byte adress of the wheel and an smbus.SMBus object used for the I2C bus.
#    It also has two optional arguments (forwardDir and rightDir) which can be either 0 or 1 (True/False).
#    These are just used to determine which direction is "forward" on the wheel motor and which direction turns the wheel to the right
#    What these need to be is really just trial and error for each wheel since it depends on how they were wired up
#
#    IMPORTANT: setDirection in the constructor is True by default. If it is set to True it will call the resetRotation function with the setDirection argument set to true.
#    resetRotation(True) tells the arduino to figure out which direction corresponds to a positive or negative encoder output
#    but the arduino must be connected AND on in order for this to do anything.
#    If the constructor is called before hooking up the arduino (in which case setDirection should be False) OR if the arduino is reset resetRotation(True) MUST BE CALLED AGAIN or the wheel rotation will not work properly.
#    If resetRotation(True) is not called setRotation() and resetRotation() will have a 50% chance of going the correct direction.
#
#   All commands asyncronously wait for the arduino to complete its task. A callback function can be specified which will be run asyncronously when the arduino finishes its task.
#   Callback functions take one argument which is True if there was an error or false if the arduino responded successfully.
#   If no callback function is specified a flag will be set for the corresponding motor.
#       For example if setRotation() is called without a callback function, then the function isTurnMotorDone() will return true when the motor has finished
#       If no callback function is specified and there is an error, responseHasFailed() will return true instead.
#
#Functions this class contatins:
#rotate(revolutions, (optional)direction, (optional) callbackFcn):
#    Rotates the wheel the specified number of rotations (can be positive or negative)
#    direction can be either "Forward" or "Backward". It just reverses the direction it rotates although I would recommend just making revolutions negative for reverse
#
#on((optional)direction):
#    Turns the wheel motor on.
#    By default direction is "Forward" but it can be set to "Backward" to move in reverse.
#
#off():
#    Turns the wheel motor off.
#
#turnWheel(deg, (optional)direction, (optional) callbackFcn):
#    Turns the wheel the specified number of degrees
#    deg is the number of degrees to turn the wheel (can be positive or negative)
#    direction can be either "Right" or "Left". It just reverses the direction it rotates although I would recommend just making deg negative for left
#
#resetRotation((optional)setDirection, (optional) callbackFcn):
#    Turns the wheel back to its starting location.
#    If setDirection is set to True it also tells the arduino to figure out which direction corresponds to a positive encoder output.
#    THIS MUST BE CALLED WITH setDirection = True BEFORE RUNNING THE NEXT TURNING FUCTIONS (If the arduino is reset since the constructor should already do this)
#
#setRotation(deg, (optional)direction, (optional) callbackFcn):
#    Rotates the wheel to the specified number of degrees from the starting position.
#    deg is the degrees to rotate to (can be negative or positive).
#    direction can be "Left" or "Right", "Right" by default.
#
#waitForResponse(finishFcn = 0):
#   Internal function. Pings the arduino every 0.05 seconds asyncronously until the arduino responds that is has successfully completed a task or it errors out.
#   finishFcn should be set to a callback function to run when the arduino responds.
#       Otherwise it will set the appropriate flag when the arduino responds.
#
#getPosition(callbackFcn = 0):
#   For speed reasons I recommend using getPositionAsync instead as the pi won't have to wait until the arduino responds to continue and should be much faster
#   returns the distance the wheel has travelled in revolutions since the arduino has turned on.
#   If callbackFcn is set to a function, it will run the function when the arduino responds with a boolean argument that is True if there was an error and False otherwise
#
#getRotation(callbackFcn = 0):
#   For speed reasons I recommend using getRotationAsync instead as the pi won't have to wait until the arduino responds to continue and should be much faster
#    Returns the rotation of the wheel in degrees from the starting position (Positive is to the right).
#   If callbackFcn is set to a function, it will run the function when the arduino responds with a boolean argument that is True if there was an error and False otherwise
#
#getPositionAsync(callbackFcn):
#   Same as getPosition but done asyncronously and requires a callback function
#
#getRotationAsync(callbackFcn):
#   Same as getRotation but done asyncronously and requires a callback function
#
#isWheelMotorDone():
#   Returns True if wheel motor has finished its task and the flag has been set, False otherwise.
#   If a callback function was specified when sending the initial command, this flag would not have been set.
#
#isTurnMotorDone():
#   Returns True if turn motor has finished its task and the flag has been set, False otherwise.
#   If a callback function was specified when sending the initial command, this flag would not have been set.
#
#responseHasFailed():
#   Returns True if a previous command has returned an error, usually due to I2C communication issues.
#   If a callback function was specified when sending the initial command, this flag would not have been set.
#
#to_bytes(num, byte = []):
#   Internal function.
#   Returns an integer as an array of bytes which can be sent over I2C


class Wheel:
    def __init__(this, address, bus, forwardDir = 0, rightDir = 0, setDirection = True):
        this.bus = bus
        this.address = address
        this.forwardDir = forwardDir
        this.rightDir = rightDir
        
        this.responseFailed = False
        this.wheelMotorResponded = False
        this.turnMotorResponded = False
        
        this.driveMotorDoneCode = 500
        this.turnMotorDoneCode = 600
        
        if setDirection:
            this.resetRotation(setDirection = True, callbackFcn = lambda a: print("Calibration Failed") if a else None)


    def rotate(this, revolutions, direction = "Forward", callbackFcn = 0):
        lowerDir = this.forwardDir
        
        if revolutions < 0:
            lowerDir = not lowerDir
            revolutions *= -1
        
        if direction == "Backward":
            lowerDir = not lowerDir
            
        if revolutions == revolutions//1:
            revolutionsBase = this.to_bytes(revolutions)
            revolutionsExponent = 0
        else:
            revolutionsBase = this.to_bytes(int(1000*revolutions))
            revolutionsExponent = -3
        
        
        this.bus.write_i2c_block_data(this.address, 0x00, [len(revolutionsBase)] + revolutionsBase + [revolutionsExponent, lowerDir])
        
        threading.Thread(target = this.waitForResponse, args = (callbackFcn,)).start()
        
    def on(this, direction = "Forward"):
        
        lowerDir = this.forwardDir
        
        if direction == "Backward":
            lowerDir = not this.forwardDir
            
        this.bus.write_i2c_block_data(this.address, 0x02, [lowerDir])
        
    def off(this):
        this.bus.write_i2c_block_data(this.address, 0x03, [])
        
    def turnWheel(this, deg, direction = "Right", callbackFcn = 0):
        lowerDir = this.rightDir
        
        if deg < 0:
            deg *= -1
            lowerDir = not lowerDir
        
        if direction == "Left":
            lowerDir = not lowerDir
            
        if deg == deg//1:
            degBase = this.to_bytes(deg)
            degExp = 0
        else:
            degBase = this.to_bytes(int(1000*deg))
            degExp = -3
            
        print(degBase, degExp)
            
        this.bus.write_i2c_block_data(this.address, 0x04, [len(degBase)] + degBase + [degExp, lowerDir])
        
        threading.Thread(target = this.waitForResponse, args = (callbackFcn,)).start()
        
    def resetRotation(this, setDirection = False, callbackFcn = 0):
        if setDirection:
            this.bus.write_i2c_block_data(this.address, 0x05, [])
        else:
            this.bus.write_i2c_block_data(this.address, 0x06, [])
            
        threading.Thread(target = this.waitForResponse, args = (callbackFcn,)).start()
            
    def setRotation(this, deg, direction = "Right", callbackFcn = 0):
        lowerDir = this.rightDir
        
        if deg < 0:
            deg *= -1
            lowerDir = not lowerDir
        
        if direction == "Left":
            lowerDir = not lowerDir
            
        if deg == deg//1:
            degBase = this.to_bytes(deg)
            degExp = 0
        else:
            degBase = this.to_bytes(int(1000*deg))
            degExp = -3
            
        this.bus.write_i2c_block_data(this.address, 0x07, [len(degBase)] + degBase + [degExp, lowerDir])
        
        threading.Thread(target = this.waitForResponse, args = (responseFcn,)).start()
        
    def waitForResponse(this, finishFcn = 0):
        timestep = 0.05
        time.sleep(timestep)
        while True:
            try:
                data = this.bus.read_i2c_block_data(this.address, 0x09, 4)
                data = int.from_bytes(data, byteorder = 'little', signed = True)
                if data == this.driveMotorDoneCode or data == this.turnMotorDoneCode:
                    break
            except:
                if isinstance(finishFcn, int):
                    this.responseFailed = True
                else:
                    finishFcn(True)
                return
            
            time.sleep(timestep)
        if isinstance(finishFcn, int):
            if data == this.driveMotorDoneCode:
                this.wheelMotorResponded = True
            elif data == this.turnMotorDoneCode:
                this.turnMotorResponded = True
            else:
                this.responseFailed = True
        else:
            finishFcn(False)
        
    def getPosition(this, callbackFcn = 0):
        data = this.bus.read_i2c_block_data(this.address, 1, 4)
        
        data = int.from_bytes(data, byteorder='little', signed = True)/1000
        
        if not this.forwardDir:
            data *= -1
            
        if isinstance(callbackFcn, int):
            return data
        else:
            callbackFcn(data)
    
    def getRotation(this, callbackFcn = 0):
        data = this.bus.read_i2c_block_data(this.address, 0x08, 4)
        
        data = int.from_bytes(data, byteorder='little', signed = True)/1000
        
        if not this.rightDir:
            data *= -1
        
        if isinstance(callbackFcn, int):
            return data
        else:
            callbackFcn(data)
    
    def getPositionAsync(this, callbackFcn):
        threading.Thread(target = this.getPosition, args = (callbackFcn,)).start()
        
    def getRotationAsync(this, callbackFcn):
        threading.Thread(target = this.getRotation, args = (callbackFcn,)).start()
    
    def isWheelMotorDone(this):
        temp = this.wheelMotorResponded
        this.wheelMotorResponded = False
        return temp
    
    def isTurnMotorDone(this):
        temp = this.turnMotorResponded
        this.turnMotorResponded = False
        return temp
    
    def responseHasFailed(this):
        temp = this.responseFailed
        this.responseFailed = False
        return temp
    
    def to_bytes(this, num, byte = []):
        if num.bit_length() <= 8:
            byte = byte + [int(bin(num)[-8:], 2)]
            print(byte)
            return byte
        byte = byte + [int(bin(num)[-8:], 2)]
        num = int(bin(num)[0:-8], 2)
        return this.to_bytes(num, byte)

#test = Wheel(0x04, smbus.SMBus(1), setDirection=True)


    
