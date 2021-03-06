/*
 * The Rad-Fi Delay 1.5
 * John-Micahel Reed 
 * BleepLabs.com
 * 
 * Glitchy delay using a teesny 3.2 and simple parts
 * https://github.com/BleepLabs/Rad-Fi-Delay-1.5
 * 
 * Adapted by Adrien Fauconnet
 * 
 */

#include <ADC.h>
ADC *adc = new ADC(); // adc object. THe adc library allows for faster analog reading
#define audio_in_pin  A2 // there are two independent ADCs in the teensy 3.2. one will be used for the audio in, the other for the pots

// pots pins
#define OSC1_POT A4
#define OSC2_POT A5
#define OSC3_POT A6
#define FB_POT A1
#define TIME_POT A0

// oscillator output pins
#define OSC1 0
#define OSC2 1
#define OSC3 2
#define RATE 13

// mode selection input pins
#define BYPASS 22
#define WET 23
#define COMBINE 6
#define FREEZE 7
#define INT1 8
#define INT2 9
#define DIV1 10
#define DIV2 11
#define DIV3 12


IntervalTimer timer1, timer2;

uint32_t dds_rate, sample_rate, d1, t1, t2, d2;
int16_t out[8] = {};
uint32_t prev[4] = {};
int16_t raw1, raw_pot[4];
float rate[4];

/*
  the delay intervals are how much to slow down the delay sampling rate
  2^16 =  65536 is full rate. Half of that, 32768, is half the rate
  these sets of values are for the three input pins
  The 
*/
byte interval_sel;
uint16_t delay_intervals[4][3] = {
  {32768, 32768 * .5, 32768 * .25},
  {32768, 32768 * 1.5, 32768 * 1.75 },
  {32768, 32768 * 1.1892, 32768 * 1.3348},  //Octave, thirds and fifths. Kind of? it sounds good
  {32768, 32768 * 1.414 , 32768 * 1.681}    //Ocatve forth and eight
};

//for square wave synth
const uint16_t waveamp = 4095;
const uint16_t wavelength = 4095;
uint32_t accumulator[4] = {};
uint32_t increment[4] = {};
uint32_t waveindex[4] = {};
byte squ[4];
byte digital_in[4];
float out_env;
int16_t dly_in, feedback;
int32_t write_loc, redux, read_head, write_head, dly_time, target_dly_time;
int16_t read_loc, fb_amt;
#define max_delay_len 12500 // size of the delay bank
int bank[max_delay_len + 1];

byte tactile[4], prev_tactile[4];
byte freeze;
byte dig_comb;
byte passthrough;
byte wet;

int16_t final_out;
int16_t delay_led, delay_led_cnt;
byte osc_tick;
int reduction_amt;
byte play_mode;
float led_max_bright = .5; //0.0 to 1.0

void setup() {
  pinMode(BYPASS, INPUT_PULLUP);
  pinMode(WET, INPUT_PULLUP);

  pinMode(COMBINE, INPUT_PULLUP);
  pinMode(FREEZE, INPUT_PULLUP);

  pinMode(INT1, INPUT_PULLUP);
  pinMode(INT2, INPUT_PULLUP);

  pinMode(DIV1, INPUT_PULLUP);
  pinMode(DIV2, INPUT_PULLUP);
  pinMode(DIV3, INPUT_PULLUP);

  pinMode(RATE, OUTPUT);
  pinMode(OSC1, OUTPUT);
  pinMode(OSC2, OUTPUT);
  pinMode(OSC3, OUTPUT);

  pinMode(audio_in_pin, INPUT);


  float dds_rate = 28;//how fast to run the delay and synth code
  sample_rate = (1.00 / (dds_rate * .000001)); //23 gives ~43.5Khz
  analogWriteResolution(12);


  //adc_1 controls pins A2 and A3, Adc_0 are the rest (on the top side of the teesny)
  adc->setAveraging(2, ADC_1); // set number of averages
  adc->setResolution(12, ADC_1); // set bits of resolution

  adc->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED, ADC_1); // change the conversion speed
  // it can be any of the ADC_MED_SPEED enum: VERY_LOW_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED or VERY_HIGH_SPEED
  adc->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED, ADC_1); // change the sampling speed

  adc->setAveraging(16, ADC_0); // set number of averages
  adc->setResolution(12, ADC_0); // set bits of resolution

  adc->setConversionSpeed(ADC_CONVERSION_SPEED::MED_SPEED, ADC_0); // change the conversion speed
  adc->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED, ADC_0); // change the sampling speed

  timer1.begin(dly1, dds_rate);
  timer1.priority(1);

}

void loop() {

  if ((millis() - prev[1]) > 20) {
    prev[1] = millis();
    /*
      Converting your linear potentiometer readings to logarkymic ones gives you a much more
      natural response whrn controlling frequency. Here the raw value is rasied to the second power
      then divided by it's top valuse, 4096 aka >>12.
      It's divide again to get it to the final range we want. Dividing is fine here since were not after speed
    */
    raw_pot[0] = analogRead(OSC1_POT);
    uint32_t logtemp = pow(raw_pot[0], 2);
    rate[0] = (logtemp >> 12) / 200.00;

    raw_pot[1] = analogRead(OSC2_POT);
    logtemp = pow(raw_pot[1], 2);
    rate[1] = (logtemp >> 12) / 200.00;

    raw_pot[2] = analogRead(OSC3_POT);
    logtemp = pow(raw_pot[2], 2);
    rate[2] = (logtemp >> 12) / 400.00;

    fb_amt = (analogRead(FB_POT) >> 4);

    /*
        If the delay time were to move around exactly with the potentiometer it would be noisey
        instead we do a linear interpolation and chase the value around.
        Having it do this more slowly creates the calssic tape delay effect
    */
    target_dly_time = analogRead(TIME_POT) * (max_delay_len / 4095.00);

  }
  //linear interpolation for dealy time
  if ((micros() - prev[2]) > 250) {
    prev[2] = micros();
    if (target_dly_time > dly_time + 10) {  //10 is there to give it a dead zone to avoid nosie
      dly_time += 2;
    }
    if (target_dly_time < dly_time - 10) {
      dly_time -= 2;
    }


  }

  if ((millis() - prev[0]) > 250) {
    prev[0] = millis();

    Serial.print(read_loc);
    Serial.print(" ");
    Serial.print(dig_comb);
    Serial.print(" ");
    Serial.println(d1); //how long the dly1 funtion takes. This needs to stay under dds_rate
    Serial.println();

  }

}
void dly1() {
  t1 = micros(); //what time did the delay funtion start
  raw1 = adc->analogRead(audio_in_pin, ADC_1) - 2048;
  //-2048 because we want the signal to be biploar, rather than jsut 0-4095
  //this way it will combine properly with other bipolar signals

  /*
    we don't need to calculate the oscillators every time this funtion is called.
    they are going to go pretty slow so and we can't run them all and have a ~44k sample rate for the delay
    so we just have each one happen ever 3rd time.
    multiplying the rate by three gives us an accurate

    A seperate timer for the oscillators could also be used but doing it this way lets us know
    exactly how long all the "fast" things take to happen.
    Also with two timers one will always have to interupt the others at some point. That won't happen this way.

  */
  osc_tick++;
  if (osc_tick == 1) {
    squ[1] = vsqu(1, rate[0] * 3, 2000);
    digitalWriteFast(OSC1, squ[1]);
  }
  if (osc_tick == 2) {
    squ[2] = vsqu(2, rate[1] * 3, 2000);
    digitalWriteFast(OSC2, squ[2]);
  }
  if (osc_tick >= 3) {
    osc_tick = 0;
    squ[3] = vsqu(3, rate[2] * 3, 2000);
    digitalWriteFast(OSC3, squ[3]);
  }
  /*
    ! means the opposite so 1 instead of 0
    A digital read returns a 1 when nothign is happending since there is an inputt pullup.
    It will return a 0 when a low signal is coming in.
  */

  digital_in[0] = !digitalRead(DIV1);
  digital_in[1] = !digitalRead(DIV2);
  digital_in[2] = !digitalRead(DIV3);


  freeze = !digitalRead(FREEZE);
  passthrough = digitalRead(BYPASS);
  dig_comb = digitalRead(COMBINE);
  interval_sel = ((!digitalRead(INT1)) << 1) + !digitalRead(INT2) ;
  wet = digitalRead(WET);

  if (freeze == 0) {  // freeze mode off
    /*
        Here a 16 bit accumulator is used to calculate how fast the delat "tape" is moving,
        which is differnt that the length of the dealy
        65536 would give us a delay at our sample rate so we subltract a set amount
        for each of the digital inputs when they are recieveing a high signal.
        half of 65536 would be half as slow
        When they are low it's jsut multiplying by 0
    */

    reduction_amt = 65536 - (digital_in[0] * delay_intervals[interval_sel][0]) - (digital_in[1] * delay_intervals[interval_sel][1]) - (digital_in[2] * delay_intervals[interval_sel][2]);
    redux += reduction_amt;

    if (redux > 65536) {
      redux -= 65536;
      write_loc++;
    }

    delay_led_cnt++;
    if (delay_led_cnt > dly_time) {
      delay_led_cnt = 0;
    }

    if (delay_led_cnt < 20) {
      digitalWrite(RATE, 1);
    }
    else {
      digitalWrite(RATE, 0);
    }

    if (write_loc > max_delay_len) {
      write_loc = 0;
    }

    read_loc = write_loc - dly_time; //the read location is at a varible location behind the write location.
    if (read_loc < 0) {
      read_loc += max_delay_len;
    }

    read_head = bank[read_loc];

    /*
      How much of the output goes back into the input is calculated by multiplying the output by a number
      then bitshifting it by 8. Same as divinging it by 256 but a bit faster.
      So if fb_amt is 128 feedback will he half the level of read_head
      if fb_amt is over 255 feedback is louder than read_head
    */
    feedback = (read_head * fb_amt) >> 8;
    if (feedback > 2047) {
      feedback -= (feedback - 2047) * 2;
    }
    if (final_out < -2047) {
      feedback = (feedback * -1);
    }

    write_head = raw1 + feedback;
    if (dig_comb == 0) { //digitally combine wet and dry to get some noise
      write_head = ((raw1 + 2048) | (feedback + 2048));
    }

    bank[write_loc] = write_head;
    if (wet == 1) {

      final_out = (read_head >> 1) + (raw1 >> 1) + 2048;
    }
    if (wet == 0) {

      final_out = (read_head )  + 2048;
    }

    if (fb_amt < 5) { // if the feedback knob is all the way down olny output dry
      //    final_out = raw1 + 2048;
    }

    if (passthrough == 0) { //only dry
      final_out = (raw1) + 2048;
    }

    // if the siganl goes out of bound fold it back around rather than just clipping it
    if (final_out > 4095) {
      final_out -= (final_out - 4095) * 2;
    }
    if (final_out < 0) {
      final_out = (final_out * -1);
    }
  }


  if (freeze == 1) {

    // if freeze mode is on we don't write to the delay bank, jsut read from it.

    reduction_amt = 65536 - (digital_in[0] * delay_intervals[interval_sel][0]) - (digital_in[1] * delay_intervals[interval_sel][1]) - (digital_in[2] * delay_intervals[interval_sel][2]);
    redux += reduction_amt;

    if (redux > 65536) {
      redux -= 65536;
      read_loc++;
    }

    if (read_loc > dly_time) { // instead of letting it loop all the way around we stop at the dealy time
      read_loc = 0;
    }

    read_head = bank[read_loc];

    final_out = read_head  + 2048;

    if (dig_comb == 0) { //digitally combine wet and dry to get some noise
      final_out = ((raw1 + 2048) | (read_head + 2048));
    }

    if (final_out > 4095) {
      final_out -= (final_out - 4095) * 2;
    }
    if (final_out < 0) {
      final_out = (final_out * -1);
    }
  }

  analogWrite(A14, final_out);

  d1 = micros() - t1; //find out how long all of this took

}

/*
  calculate a varible square wave using a standard DDS, diriect digital synthesis method
  "sel" should be diffent for each differnt wave we want to make
  freq ia the output frequency in Hz
  pulse_width_in is a number between 0-4095 taht defines the duty aka pulse width
*/
int16_t vsqu(byte sel, float freq, uint16_t pulse_width_in) {
  static int16_t tout;
  static uint16_t pulse_width;
  pulse_width = pulse_width_in;

  if (pulse_width < 0)
  {
    pulse_width = 0;
  }

  if (pulse_width > wavelength - 1)
  {
    pulse_width = wavelength - 1;
  }
  // this is wat determains the frequency. Bacailly we step through our 4096 step waveindex, sometimes pausing on a value, somethimes skipping one to acive the desired frequency

  increment[sel] = (4294967296.00 * (freq)) / (sample_rate); // 2^32 * freq we want in Hz / sample rate of timer in Hz
  accumulator[sel] += increment[sel];
  waveindex[sel] = ((accumulator[sel]) >> (20)); //shift by 20 as we jsut want 12 of our 32 bits as the wavelength is 12bits (4096) values long

  if (waveindex[sel] < pulse_width) {
    tout = 1;  //we could have this refer to a wave table but we just want ons and offs
  }
  else {
    tout = 0;
  }
  return tout ;
}
