# foot-pressure-system
This contains the details to the foot pressure measurement system application. 

bme261_code.ino --> code that gets updated. Last update: 03/24/26 @ around 1:00pm
- this code will read + get V changes from 3x4 copper wires
- button just turns on/off device and automatically tries to connect to Windows app
- transmit the data through Bluetooth
- user has to use the app interface to start and begin recording session


## Data Transmission Protocol & Payload Structure

The ESP32 firmware is designed to continuously sample the 3x4 sensor matrix at a rate of 100 Hz[cite: 157, 166]. During every 10-millisecond cycle, the microcontroller reads all 12 physical nodes on the sandal, calculates the current battery percentage, and packages this into a single data payload.

Before the payload is padded and secured via AES-128 encryption [cite: 142] for Bluetooth transmission, it is serialized into a simple, comma-separated string. 

### Example Raw Packet
A raw, unencrypted data packet looks like this:
`0,150,0,4095,3000,500,0,0,10,0,0,0,85`

### Payload Breakdown
The string always contains exactly 13 values separated by commas. 

* **Indices 0-11 (The Matrix):** These are the raw analog pressure readings from the Velostat layer. The ESP32 uses a 12-bit ADC, meaning the pressure values will always fall within a range of `0` to `4095`.
    * `0` = No pressure applied (High resistance).
    * `4095` = Maximum pressure applied (Velostat fully compressed).
* **Index 12 (The Battery):** The final value is the estimated battery percentage, mapped to a standard `0` to `100` scale. This allows the receiving application to trigger a low-battery alert.

### Data Dictionary: Matrix Index Mapping
When the Windows application receives, decrypts, and splits the string by commas (`,`), it must parse the resulting array to build the 2D heat map. The data is generated sequentially by scanning columns (0 to 2) and reading rows (0 to 3). 

Use this map to tie the array index to the physical nodes on the footplate:

| Array Index | Grid Position | Suggested Physical Mapping (Adjust as needed) |
| :--- | :--- | :--- |
| `[0]` | Col 0, Row 0 | Outer Heel |
| `[1]` | Col 0, Row 1 | Outer Midfoot |
| `[2]` | Col 0, Row 2 | Outer Ball |
| `[3]` | Col 0, Row 3 | Pinky Toe |
| `[4]` | Col 1, Row 0 | Center Heel |
| `[5]` | Col 1, Row 1 | Arch / Center Midfoot |
| `[6]` | Col 1, Row 2 | Center Ball |
| `[7]` | Col 1, Row 3 | Middle Toes |
| `[8]` | Col 2, Row 0 | Inner Heel |
| `[9]` | Col 2, Row 1 | Inner Midfoot |
| `[10]` | Col 2, Row 2 | Inner Ball |
| `[11]` | Col 2, Row 3 | Big Toe |
| `[12]` | **Battery** | System Power (%) |

**Note for Application Processing:** Because the string length fluctuates depending on the size of the ADC numbers (e.g., `0` vs `4095`), the total byte length of the packet changes. The ESP32 firmware pads the string with empty space to reach a perfect multiple of 16 bytes before applying the AES-128 encryption block cipher. The receiving software should trim any trailing null characters after decryption.
