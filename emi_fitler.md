# FM Radio Power and EMI Schematic

This Mermaid diagram redraws the supplied power schematic in a responsive format for GitHub. It keeps the original two supply branches and component values.

```mermaid
flowchart TB
    PWR["LX-2BUPS Power Bank<br/>+5V / GND"]
    CHOKE["Common-mode choke<br/>10-20 turns on one toroidal core"]
    BULK5["Stage 1: 5V bulk filtering<br/>2 x 470 uF / 16V in parallel"]
    MAIN["+5V MAIN / STAR GND"]

    PWR --> CHOKE --> BULK5 --> MAIN

    subgraph RADIO["BRANCH A - SI4703 RADIO / CLEAN LDO SUPPLY"]
        direction TB
        A5["+5V MAIN"] --> LDO["AMS1117-3.3"] --> LDO_RAIL["+3.3V LDO rail"]
        LDO_RAIL --> L1["L1: 10 uH series inductor / ferrite choke"] --> RF_RAIL["+3.3V RF rail"]
        RF_RAIL --> SI4703["SI4703 FM module<br/>VCC / GND"]

        LDO_RAIL -.-> A_BULK["Stage 1 bulk capacitors<br/>2 x 100 uF electrolytic<br/>in parallel to Radio GND"]
        LDO_RAIL -.-> A_PRE["Stage 2 input bypass<br/>100 nF ceramic to Radio GND"]
        RF_RAIL -.-> A_POST["Stage 2 output bypass<br/>100 nF ceramic to Radio GND"]
        SI4703 -.-> A_LOCAL["Stage 3 local decoupling<br/>2 x 10 uF tantalum to Radio GND"]
        A_BULK --> RADIO_GND["Radio GND"]
        A_PRE --> RADIO_GND
        A_POST --> RADIO_GND
        A_LOCAL --> RADIO_GND
    end

    subgraph DIGITAL["BRANCH B - LOGIC AND AMPLIFIER"]
        direction TB
        B5["+5V MAIN"] --> MP2307["MP2307 step-down regulator<br/>set to 3.3V"]
        MP2307 --> BYPASS["100 nF ceramic bypass<br/>SPI / regulator output"] --> D3V3["+3.3V DIGITAL rail"]
        D3V3 --> TFT["ILI9341 TFT display"]
        D3V3 --> AMP["PAM8403 amplifier<br/>as shown in supplied schematic"]
        D3V3 -.-> D_CAPS["Output capacitors in parallel to STAR GND<br/>2 x 470 uF + 1 x 100 nF"]
        D_CAPS --> STAR_GND["STAR GND"]
    end

    MAIN --> A5
    MAIN --> B5
    MAIN --> ESP5["ESP32-S3 5VCC / VIN"]
    STAR_GND --> ESP_GND["ESP32-S3 GND"]
    STAR_GND --- RADIO_GND
    STAR_GND --- TFT
    STAR_GND --- AMP

    SI4703 -->|"L-OUT / R-OUT, shielded cable"| AMP_IN["PAM8403 L-IN / R-IN"]
    SI4703 -->|"AGND"| AMP_GND["PAM8403 Audio GND"]
    SI4703 -->|"FM_IN"| ANT1["Telescopic antenna 1<br/>72.5 cm"]
    SI4703 -->|"RF return / counterpoise"| ANT2["Telescopic antenna 2<br/>72.5 cm"]

    classDef source fill:#243447,stroke:#7aa2c7,color:#ffffff
    classDef radio fill:#173b35,stroke:#55b89c,color:#ffffff
    classDef digital fill:#34304a,stroke:#a69bd2,color:#ffffff
    classDef ground fill:#3b3b3b,stroke:#aaaaaa,color:#ffffff
    class PWR,CHOKE,BULK5,MAIN source
    class A5,LDO,LDO_RAIL,L1,RF_RAIL,SI4703,A_BULK,A_PRE,A_POST,A_LOCAL,ANT1,ANT2 radio
    class B5,MP2307,BYPASS,D3V3,TFT,AMP,D_CAPS,ESP5,AMP_IN,AMP_GND digital
    class RADIO_GND,STAR_GND,ESP_GND ground
```

## Notes

- Capacitors shown as bypass, bulk, or local decoupling are connected in parallel between their supply node and the indicated ground. They are not in series with the supply rail.
- L1 is in series with the positive SI4703 supply rail; do not put an inductor in the ground return.
- The supplied schematic shows PAM8403. It is not installed in the current radio build, which uses external powered speakers.
- Keep the MP2307 switching regulator and its inductor physically separated from the SI4703 module and antenna.
- Confirm regulator polarity, capacitor polarity, and the voltage requirements of each breakout before powering the circuit.
