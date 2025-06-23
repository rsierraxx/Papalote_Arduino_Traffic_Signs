
const int Button_1=1;
const int Led_1=2;

const int Button_2=3;
const int Led_2=4;

const int Button_3=5;
const int Led_3=6;

int Button_1_State = 0;
int prevButton_1_State = 0;

int Button_2_State = 0;
int prevButton_2_State = 0;

int Button_3_State = 0;
int prevButton_3_State = 0;

void setup() {
  
  pinMode(Led_1,OUTPUT);
  pinMode(Button_1,INPUT);
  
  pinMode(Led_2,OUTPUT);
  pinMode(Button_2,INPUT);
  
  pinMode(Led_3,OUTPUT);
  pinMode(Button_3,INPUT);
  
  
}

void loop(){
  
  Button_1_State = digitalRead(Button_1);
  Button_2_State = digitalRead(Button_2);
  Button_3_State = digitalRead(Button_3);
  
  if(Button_1_State == HIGH) {
    if (Button_1_State != prevButton_1_State) {
      digitalWrite(Led_1,HIGH);
      delay(20);
      prevButton_1_State = Button_1_State;
    }
  }else{
    digitalWrite(Led_1,LOW);
    prevButton_1_State = Button_1_State;
  }
  
  
  if(Button_2_State == HIGH) {
    if (Button_2_State != prevButton_2_State) {
      digitalWrite(Led_2,HIGH);
      delay(20);
      prevButton_2_State = Button_2_State;
    }
  }else{
    digitalWrite(Led_2,LOW);
    prevButton_2_State = Button_2_State;
  }
  
  
  if(Button_3_State == HIGH) {
    if (Button_3_State != prevButton_3_State) {
      digitalWrite(Led_3,HIGH);
      delay(20);
      prevButton_3_State = Button_3_State;
    }
  }else{
    digitalWrite(Led_3,LOW);
    prevButton_3_State = Button_3_State;
  }

  delay(20); 
  
}
  