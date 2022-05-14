#include <Arduino.h>
#include "HX711.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "app_config.h"

// MQTT client
WiFiClientSecure https_client;
PubSubClient mqtt_client(https_client);

// Monitor device
#define WEIGHT_DEVICE "WeightMonitor"

// MQTT topic
#define TOPIC_BASE "catsensor/"
#define TOPIC_WEIGHT_DATA TOPIC_BASE "weight_data/" WEIGHT_DEVICE
#define TOPIC_STATUS TOPIC_BASE "status/" WEIGHT_DEVICE


// HX711 circuit wiring
#define LOADCELL_1_DOUT_PIN 32
#define LOADCELL_1_SCK_PIN 33
#define LOADCELL_2_DOUT_PIN 14
#define LOADCELL_2_SCK_PIN 12

// Scale
#define DETECT_INTERVAL 1000 * 1 // 通常計測の間隔(1000 msec)
#define CALIBRATION_INTERVAL 100 // キャリブレーション時の間隔(100 msec)
#define CALIBRATION_TIMES 5 // キャリブレーションの回数
#define CALIBRATION_RESET_COUNT 5 // 再キャリブレーションを実行すると判断する回数
#define WEIGHT_PER_GRAM 419.527 // センサーのgあたりの数値
#define TRIGGER_THRESHOLD_GRAMS 1000 // 猫が乗り降りしたと判断する重さ(gram)
#define CALIBRATION_THRESHOLD_GRAMS 30 // キャリブレーションをやり直す重さ(gram)
#define SESSION_DURATION_THRESHOLD 15 // seconds 体重判定のタイミング

HX711 scale1;
HX711 scale2;
int loop_interval = DETECT_INTERVAL;
long scale_calibration = 0; //センサー全体のキャリブレーション用
long calibration_weight = 0; //センサー全体のキャリブレーション用
int calibration_count = 0; // キャリブレーション時のデータ取得回数
int calibration_reset_count = 0; // 再キャリブレーションをすると判断する回数
bool calibration_complete = false; // キャリブレーション完了
bool session_start = false; // トイレ開始
int session_duration = 0; // トイレと判断したあとの継続時間
float weigth_grams[SESSION_DURATION_THRESHOLD]; // トイレ中の重さを保存

/**
 * @send_status
 * ステータスをAWS IoT Coreに送信する
 * @param msg メッセージ
 */
void send_status(String message){
    StaticJsonDocument<200> doc;
    doc["message"] = message.c_str();
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    mqtt_client.publish(TOPIC_STATUS, jsonBuffer);
    Serial.print("ステータス送信:");
    Serial.println(jsonBuffer);
}

/**
 * Connect to WiFi access point
 **/
void connect_wifi()
{
//    Serial.print("Connecting: ");
//    Serial.println(APP_CONFIG_WIFI_SSID);

    WiFi.disconnect(true);
    delay(1000);

    WiFi.begin(APP_CONFIG_WIFI_SSID, APP_CONFIG_WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }
    Serial.println("WiFi Connected");
//    Serial.print("IPv4: ");
//    Serial.println(WiFi.localIP().toString().c_str());
}

/**
 * Reconnect to WiFi access point
 **/
void reconnect_wifi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connect_wifi();
    }
}

/**
 * Initialize MQTT connection
 **/
void init_mqtt()
{
    https_client.setCACert(AWS_ROOT_CA_CERTIFICATE);
    https_client.setCertificate(AWS_IOT_CERTIFICATE);
    https_client.setPrivateKey(AWS_IOT_PRIVATE_KEY);
    mqtt_client.setServer(AWS_IOT_ENDPOINT, AWS_IOT_MQTT_PORT);
    mqtt_client.setBufferSize(AWS_IOT_MQTT_MAX_PAYLOAD_SIZE);
}

/**
 * Connect to AWS IoT
 **/
void connect_awsiot()
{
    reconnect_wifi();

    while (!mqtt_client.connected())
    {
        Serial.println("Start MQTT connection...");
        if (mqtt_client.connect(AWS_IOT_THING_NAME)) {
            Serial.println("connected");
            String status("connected");
            send_status(status);
        }
        else {
            Serial.print("[WARNING] failed, rc=");
            Serial.print(mqtt_client.state());
            Serial.println("try again in 5 seconds.");
            delay(5000);
        }
    }
}

/**
 * @initialize_sensor
 * センサーの初期化
 */
void initialize_sensor(){
    scale1.begin(LOADCELL_1_DOUT_PIN, LOADCELL_1_SCK_PIN);
    scale2.begin(LOADCELL_2_DOUT_PIN, LOADCELL_2_SCK_PIN);

    // initialize
    scale_calibration = 0;
    calibration_count = 0;
    calibration_complete = false;
}

/**
 * Initial device setup
 **/
void setup() {
    Serial.begin(115200);

    initialize_sensor();

    connect_wifi();
    init_mqtt();
}

/**
 * @send_weight
 * 体重をAWS IoT Coreに送信する
 * @param weight 体重
 */
void send_weight(float weight){

    StaticJsonDocument<200> doc;
    doc["weight"] = weight;
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    mqtt_client.publish(TOPIC_WEIGHT_DATA, jsonBuffer);
    Serial.print("メッセージ送信:");
    Serial.println(jsonBuffer);
}

/**
 * @sort_desc
 * 大きいかを判断
 * @param p_n1 1つ目の値
 * @param p_n2 3つ目の値
 * @return -1,0,1
 */
int sort_desc(const void *p_n1, const void *p_n2) {
    int ret;
    if (*(float *)p_n1 < *(float *)p_n2) {
        ret = -1;
    } else if (*(float *)p_n1 > *(float *)p_n2) {
        ret = 1;
    } else {
        ret = 0;
    }
    return ret;
}

/**
 * @get_weight
 * 飛び乗ったりすると、実際の体重より大きい数字となるので、
 * 最大の2値を除く5件の平均を体重とする
 * @param weight 体重の配列
 * @return 平均の体重
 */
float get_weight(float *weight){

    qsort(weight, sizeof(*weight) / sizeof(weight[0]), sizeof(float), sort_desc);
    float weight_total = 0.0;
    for (int i = 2; i< sizeof(*weight)-2; i++){
        weight_total = weight_total + weight[i];
    }
    return weight_total / 5;
}

void loop() {
    loop_interval = DETECT_INTERVAL;

    connect_awsiot();

    mqtt_client.loop();

    if (scale1.is_ready() && scale2.is_ready()) {
        long total_weight = scale1.read_average(3) + scale2.read_average(3);
        //Serial.print("total_weight:");
        //Serial.println(total_weight);

        if(calibration_complete){
            long weight_diff = total_weight - calibration_weight;
            bool increased_weight = true;
            if (weight_diff < 0){
                // 重さが減った
                weight_diff = labs(weight_diff);
                increased_weight = false;
            }
            float grams_diff = weight_diff / WEIGHT_PER_GRAM;
            if(session_start){
                Serial.print("トイレ中 重さ:");
                Serial.print(grams_diff);
                Serial.println(" g");
                weigth_grams[session_duration] = grams_diff;
                session_duration++;
                if(session_duration > SESSION_DURATION_THRESHOLD){
                    float weight = get_weight(weigth_grams);
                    if( weight > TRIGGER_THRESHOLD_GRAMS){
                        // 猫が乗ったと判断
                        Serial.print("体重:");
                        Serial.println(weight);
                        send_weight(weight);
                    }
                    else{
                        // 猫砂を追加したと判断
                        Serial.print("猫砂を追加しただけ:");
                        Serial.println(weight);
                    }
                    // ベースラインを今の重さに変更
                    session_start = false;
                    session_duration = 0;
                }
            }
            else{
                if(increased_weight && grams_diff > TRIGGER_THRESHOLD_GRAMS){
                    // 猫が乗った可能性あり
                    Serial.println("猫が乗った可能性あり");
                    session_start = true;
                }
                else if(grams_diff > CALIBRATION_THRESHOLD_GRAMS){
                    calibration_reset_count++;
                    if(calibration_reset_count > CALIBRATION_RESET_COUNT){
                        Serial.print("キャリブレーションやり直し diff:");
                        Serial.println(weight_diff);
                        // 猫砂追加の可能性があるので、キャリブレーションをやり直し
                        calibration_count = 0;
                        scale_calibration = 0;
                        calibration_complete = false;
                    }
                }
            }
        }
        else{
            Serial.print("キャリブレーション中:");
            Serial.println(calibration_count);
            calibration_count++;
            scale_calibration = scale_calibration + total_weight;
            if(calibration_count >= CALIBRATION_TIMES){
                calibration_weight = scale_calibration / CALIBRATION_TIMES;
                calibration_count = 0;
                scale_calibration = 0;
                calibration_complete = true;
                Serial.print("キャリブレーション完了 基準重量:");
                Serial.println(calibration_weight);
                loop_interval = CALIBRATION_INTERVAL;
                String status("calibration done.");
                send_status(status);
            }
        }
    } else {
        String status("Scale not found.");
        Serial.println(status);
        send_status(status);
        initialize_sensor();
   }

    delay(loop_interval);
}