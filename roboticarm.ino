#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <AccelStepper.h>

// WiFi Access Point settings
const char* ap_ssid = "RoboticArmAP";
const char* ap_password = "12345678";

// Servo objects (continuous rotation)
Servo link1Servo;
Servo link2Servo;
Servo link3Servo;  // Gripper servo

// Servo pins
const int link1Pin = 19;
const int link2Pin = 21;
const int link3Pin = 23;  // Gripper pin

// Continuous servo control values (90 = stop)
int link1Speed = 90;
int link2Speed = 90;
int link3Speed = 90;  // Gripper position (0-180)

// Last movement timers
unsigned long link1LastMove = 0;
unsigned long link2LastMove = 0;
unsigned long link3LastMove = 0;
const unsigned long STOP_DELAY = 100;  // 0.5 seconds

// Stepper motor setup
#define STEP_PIN 32
#define DIR_PIN 26
#define ENABLE_PIN 2
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
int stepperSpeed = 0;

// Deadzone (10%)
const int DEADZONE = 10;

// Create WebServer object on port 80
WebServer server(80);

const char html_head[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Robotic Arm Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    body {max-width: 800px; margin:0px auto; padding:20px; background:#f5f5f5;}
    .container {background:white; padding:20px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.1);}
    .control-panel {display:flex; flex-wrap:wrap; justify-content:space-around; gap:20px; margin-bottom:20px;}
    .joystick-container, .slider-container {flex:1; min-width:200px;}
    .joystick {width:200px; height:200px; background:#eee; border-radius:50%; position:relative; margin:0 auto; touch-action:none;}
    .joystick-head {width:60px; height:60px; background:#04AA6D; border-radius:50%; position:absolute; top:70px; left:70px;}
    .slider {width:100%; height:25px; background:#d3d3d3; outline:none;}
    .slider::-webkit-slider-thumb {width:25px; height:25px; background:#04AA6D; cursor:pointer;}
    .value-display {font-weight:bold; color:#04AA6D; margin:10px 0;}
    .ip-address {margin-top:20px; font-size:1rem; color:#666;}
  </style>
</head>
<body>
  <div class="container">
    <h2>Robotic Arm Control</h2>
    
    <div class="control-panel">
      <div class="joystick-container">
        <h3>Link 1 Control</h3>
        <div id="joystick1" class="joystick">
          <div class="joystick-head"></div>
        </div>
        <p>Speed: <span id="link1Value" class="value-display">0%</span></p>
      </div>
      
      <div class="joystick-container">
        <h3>Link 2 Control</h3>
        <div id="joystick2" class="joystick">
          <div class="joystick-head"></div>
        </div>
        <p>Speed: <span id="link2Value" class="value-display">0%</span></p>
      </div>
      
      <div class="joystick-container">
        <h3>Turntable Control</h3>
        <div id="joystick3" class="joystick">
          <div class="joystick-head"></div>
        </div>
        <p>Speed: <span id="stepperValue" class="value-display">0%</span></p>
      </div>
    </div>
    
    <div class="slider-container">
      <h3>Gripper Control</h3>
      <p>Position: <span id="link3Value" class="value-display">90°</span></p>
      <input type="range" min="0" max="180" value="90" class="slider" id="link3Slider" oninput="updateLink3(this.value)">
    </div>


    <div class="ip-address">
      <p>Connected to: RoboticArmAP</p>
      <p>Control at: 192.168.4.1</p>
    </div>
  </div>

  <script>
    const deadzone = 10;
    const stopDelay = 500; // 0.5 seconds
    let link1Timer, link2Timer, stepperTimer;

    const joysticks = [
      { element: document.getElementById('joystick1'), head: document.querySelector('#joystick1 .joystick-head'), 
        display: document.getElementById('link1Value'), type: 'link1', timer: null },
      { element: document.getElementById('joystick2'), head: document.querySelector('#joystick2 .joystick-head'), 
        display: document.getElementById('link2Value'), type: 'link2', timer: null },
      { element: document.getElementById('joystick3'), head: document.querySelector('#joystick3 .joystick-head'), 
        display: document.getElementById('stepperValue'), type: 'stepper', timer: null }
    ];

    function setupJoystick(joystick) {
      let active = false;
      let currentPos = { x: 100, y: 100 };

      function updatePosition(clientX, clientY) {
        const rect = joystick.element.getBoundingClientRect();
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        const deltaX = clientX - centerX;
        const deltaY = clientY - centerY;
        const distance = Math.min(Math.sqrt(deltaX * deltaX + deltaY * deltaY), 100);
        const angle = Math.atan2(deltaY, deltaX);

        currentPos.x = 100 + Math.cos(angle) * distance;
        currentPos.y = 100 + Math.sin(angle) * distance;
        joystick.head.style.left = (currentPos.x - 30) + 'px';
        joystick.head.style.top = (currentPos.y - 30) + 'px';

        // Calculate speed (-100 to 100) based on Y position (inverted)
        let speed = Math.round((100 - currentPos.y) / 100 * 100);
        
        // Apply deadzone
        if (Math.abs(speed) < deadzone) speed = 0;
        
        // Update display
        joystick.display.textContent = speed + '%';
        
        // Send to ESP32 and reset timer
        fetch('/' + joystick.type + '?speed=' + speed);
        resetTimer(joystick);
      }

      function resetTimer(joystick) {
        if (joystick.timer) clearTimeout(joystick.timer);
        joystick.timer = setTimeout(() => {
          joystick.display.textContent = '0%';
          fetch('/' + joystick.type + '?speed=0');
          currentPos = { x: 100, y: 100 };
          joystick.head.style.left = '70px';
          joystick.head.style.top = '70px';
        }, stopDelay);
      }

      function handleStart(e) {
        e.preventDefault();
        active = true;
        updatePosition(e.clientX || e.touches[0].clientX, e.clientY || e.touches[0].clientY);
      }

      function handleMove(e) {
        if (!active) return;
        e.preventDefault();
        updatePosition(e.clientX || e.touches[0].clientX, e.clientY || e.touches[0].clientY);
      }

      function handleEnd() {
        active = false;
        joystick.display.textContent = '0%';
        fetch('/' + joystick.type + '?speed=0');
        currentPos = { x: 100, y: 100 };
        joystick.head.style.left = '70px';
        joystick.head.style.top = '70px';
      }

      // Event listeners
      joystick.element.addEventListener('mousedown', handleStart);
      document.addEventListener('mousemove', handleMove);
      document.addEventListener('mouseup', handleEnd);
      joystick.element.addEventListener('touchstart', handleStart);
      document.addEventListener('touchmove', handleMove);
      document.addEventListener('touchend', handleEnd);
    }

    function updateLink3(speed) {
  document.getElementById('link3Value').textContent = speed + '%';
  fetch('/link3?speed=' + speed);
  }
    // Initialize all joysticks
    joysticks.forEach(setupJoystick);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  String html = FPSTR(html_head);
  server.send(200, "text/html", html);
}

int applyDeadzone(int input) {
  if (abs(input) < DEADZONE) return 0;
  return input > 0 ? map(input, DEADZONE, 100, 0, 100) : map(input, -DEADZONE, -100, 0, -100);
}

void setup() {
  Serial.begin(115200);
  
  // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  // Attach servos
  link1Servo.attach(link1Pin);
  link2Servo.attach(link2Pin);
  link3Servo.attach(link3Pin);  // Gripper servo
  
  // Set initial positions
  link1Servo.write(90);
  link2Servo.write(90);
  link3Servo.write(90);

  link1Speed = constrain(map(applyDeadzone(server.arg("speed").toInt()), -100, 100, 75, 105), 75, 105);
  link2Speed = constrain(map(applyDeadzone(server.arg("speed").toInt()), -100, 100, 75, 105), 75, 105);
  link3Speed = constrain(map(applyDeadzone(server.arg("speed").toInt()),  -100, 100, 0, 180), 0, 180);
  
  // Stepper motor setup
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);
  
  // Create WiFi Access Point
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Server routes
  server.on("/", handleRoot);
  
  server.on("/link1", []() {
    if (server.hasArg("speed")) {
      link1Speed = map(applyDeadzone(server.arg("speed").toInt()), -100, 100, 0, 180);
      link1LastMove = millis();
      server.send(200, "text/plain", "OK");
    }
  });
  
  server.on("/link2", []() {
    if (server.hasArg("speed")) {
      link2Speed = map(applyDeadzone(server.arg("speed").toInt()), -100, 100, 0, 180);
      link2LastMove = millis();
      server.send(200, "text/plain", "OK");
    }
  });
  
    server.on("/link3", []() {
    if (server.hasArg("speed")) {
      link3Speed = map(applyDeadzone(server.arg("speed").toInt()), -100, 100, 0, 180);
      link3LastMove = millis();
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/stepper", []() {
    if (server.hasArg("speed")) {
      stepperSpeed = applyDeadzone(server.arg("speed").toInt());
      server.send(200, "text/plain", "OK");
    }
  });
  
  server.begin();
}

void loop() {
  server.handleClient();
  
  // Auto-stop servos after 0.5 seconds
  if (millis() - link1LastMove > STOP_DELAY) {
    link1Speed = 90;
  }
  if (millis() - link2LastMove > STOP_DELAY) {
    link2Speed = 90;
  }
  if (millis() - link3LastMove > STOP_DELAY) {
    link3Speed = 90;
  }
  
  // Update servos
  link1Servo.write(constrain(link1Speed, 75, 105));
  link2Servo.write(constrain(link2Speed, 75, 105));
  link3Servo.write(constrain(link3Speed, 0, 180));
  
  // Control stepper motor
  if (stepperSpeed > 0) {
    stepper.setSpeed(map(stepperSpeed, 0, 100, 0, 500));
  } else if (stepperSpeed < 0) {
    stepper.setSpeed(map(stepperSpeed, -100, 0, -500, 0));
  } else {
    stepper.setSpeed(0);
  }
  stepper.runSpeed();
  
  delay(10);
}