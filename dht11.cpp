#include <iostream>
#include <pigpiod_if2.h>
#include <unistd.h>
#include <vector>
#include <cstdint>
#include <climits>
#include <chrono>
#include <thread>

// 定数定義
#define DHT11_DATAPIN 4         // データ受信PIN番号
#define DHT11_START_SIG_LOW 18  // スタートLOWシグナル (ms)
#define DHT11_START_SIG_HIGH 40 // スタートHIGHシグナル (µs)

uint32_t last_rising_tick = 0;
std::vector<int> pulse_lengths;
// コールバック関数
void cb(int pi, unsigned gpio, unsigned level, uint32_t tick)
{
    if (level == 1)
    {
        last_rising_tick = tick; // 立ち上がり時刻を記録
    }
    else if (level == 0)
    {
        if (last_rising_tick != 0)
        {
            int pulse_length = tick - last_rising_tick; // 立ち下がり時刻から立ち上がり時刻を引いてパルス幅を算出
            pulse_lengths.push_back(pulse_length);      // パルス幅を配列に格納
            last_rising_tick = 0;                       // リセット
        }
    }
}

bool readDHT11(int pi, int pin)
{
    // スタート信号
    set_mode(pi, pin, PI_OUTPUT);
    gpio_write(pi, pin, PI_LOW);
    usleep(DHT11_START_SIG_LOW * 1000); // 18 ms
    gpio_write(pi, pin, PI_HIGH);
    usleep(DHT11_START_SIG_HIGH); // 40 µs
    set_mode(pi, pin, PI_INPUT);

    return true;
}

std::vector<bool> calculateBits(const std::vector<int> &pullUpLengths)
{
    int shortestPullUp = INT_MAX;
    int longestPullUp = 0;

    for (int length : pullUpLengths) // 最も長く1が入力された回数、最も短かった回数を抽出
    {
        if (length < shortestPullUp)
            shortestPullUp = length;
        if (length > longestPullUp)
            longestPullUp = length;
    }

    float halfway = shortestPullUp + (longestPullUp - shortestPullUp) / 2.0; // 1が入力された回数の平均
    bool bit;
    std::vector<bool> bits;

    for (int length : pullUpLengths)
    {
        bit = false;
        if (length > halfway)
        { // 1の個数が平均より多ければ1と判定
            bit = true;
        }
        bits.push_back(bit); // 配列にビットを格納
        // std::cout << bit << std::endl;
    }

    return bits;
}

std::vector<uint8_t> bitsToBytes(const std::vector<bool> &bits)
{
    std::vector<uint8_t> theBytes;
    uint8_t byte = 0;

    for (size_t i = 0; i < bits.size(); ++i)
    {
        byte = (byte << 1) | (bits[i] ? 1 : 0);

        if ((i + 1) % 8 == 0)
        {
            theBytes.push_back(byte);
            byte = 0;
        }
    }

    return theBytes;
}

uint8_t calculateChecksum(const std::vector<uint8_t> &theBytes)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        checksum += theBytes[i];
    }
    return checksum & 255;
}

int main()
{
    int pi = pigpio_start(NULL, NULL); // pigpioデーモンに接続
    if (pi < 0)
    {
        std::cerr << "Failed to connect to pigpio daemon" << std::endl;
        return 1;
    }

    // DHT11からのデータ受信開始
    readDHT11(pi, DHT11_DATAPIN);

    // コールバック設定
    callback(pi, DHT11_DATAPIN, EITHER_EDGE, cb);

    // メインループ
    while (true)
    {
        if (pulse_lengths.size() == 40)
        {
            std::vector<uint8_t> theBytes = bitsToBytes(calculateBits(pulse_lengths));
            uint8_t checksum = calculateChecksum(theBytes);

            if (theBytes[4] != checksum)
            {
                std::cout << "error" << std::endl;
            }
            float temperature = theBytes[2] + static_cast<float>(theBytes[3]) / 10;
            float humidity = theBytes[0] + static_cast<float>(theBytes[1]) / 10;
            std::cout << "Temperature:" << temperature << "C" << "," << "Himidity:" << humidity << "%" << std::endl;
        }
        sleep(2);
    }

    pigpio_stop(pi); // pigpioの終了

    return 0;
}
