# Pancake CNC – Channelization Rev2

This document describes the channelization for the Pancake CNC.
It details signal routing from the CNC controller, through the PCB,  
through ports on the PCB, and to the end effectors.

This pertains to the second revision of the Pancake CNC PCB.

---

| End User                | Direction   | PCB Port                              | Box Port   | Corrected Box Port Position   | Position   | Internal Wire   | Conditioning   | ESP32 Pin   | ESP32 Pin Function   | Pi PIN   |
|:------------------------|:------------|:--------------------------------------|:-----------|:------------------------------|:-----------|:----------------|:---------------|:------------|:---------------------|:---------|
| Servo Control           | Output      | Aux DSub (Warning, flipped connector) | DSUB7      | 8                             | 7          | Brown           | 5V Level Shift | GPIO12      |                      |          |
| S1 Limit                | Input       | Aux DSub (Warning, flipped connector) | DSUB1      | 5                             | 1          | Blue Stripe     | None           | GPIO15      |                      |          |
| S0 Limit                | Input       | Aux DSub (Warning, flipped connector) | DSUB3      | 3                             | 3          | Green Strip     | None           | GPIO16      |                      |          |
| Pump Pulse              | Output      | JST to Driver Zone                    |            |                               | 1          |                 | None           | GPIO04      | pulse                |          |
| Pump Dir                | Output      | JST to Driver Zone                    |            |                               | 2          |                 | None           | GPIO05      | dir                  |          |
| Pump Enable             | Output      | JST to Driver Zone                    |            |                               | 3          |                 | None           | GPIO06      | enable               |          |
| S0 Pulse                | Output      | JST to Driver Zone                    |            |                               | 4          |                 | None           | GPIO07      | pulse                |          |
| S0 Dir                  | Output      | JST to Driver Zone                    |            |                               | 5          |                 | None           | GPIO08      | dur                  |          |
| S1 Pulse                | Output      | JST to Driver Zone                    |            |                               | 6          |                 | None           | GPIO09      | pulse                |          |
| S1 Dir                  | Output      | JST to Driver Zone                    |            |                               | 7          |                 | None           | GPIO10      | dir                  |          |
| S0/S1 Enable            | Output      | JST to Driver Zone                    |            |                               | 8          |                 | None           | GPIO14      | enbable              |          |
| Input Pot X             | Input       | JST to UI Zone                        |            |                               |            |                 | None           | GPIO01      | ADC1_CH0             |          |
| Input Pot Y             | Input       | JST to UI Zone                        |            |                               |            |                 | None           | GPIO02      | ADC1_CH1             |          |
| Flip Switch 1           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO18      |                      |          |
| Control Box LED Address | Output      | JST to UI Zone                        |            |                               |            |                 | 5V Level Shift | GPIO21      |                      |          |
| Flip Switch 2           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO38      |                      |          |
| Flip Switch 3           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO45      |                      |          |
| Flip Switch 4           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO46      |                      |          |
| Flip Switch 5           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO47      |                      |          |
| Flip Switch 6           | Output      | JST to UI Zone                        |            |                               |            |                 |                | GPIO48      |                      |          |
| Control Box Temp        | Input       | None                                  |            |                               |            |                 |                | GPIO03      | ADC1_CH3             |          |
| PCB Alive LED           | Output      | None                                  |            |                               |            |                 | None           | GPIO17      |                      |          |
| Pi Serial Data In       | Input       | Pi JST                                |            |                               | 1          |                 |                | GPIO11      | FSPID                |          |
| Pi Serial Data Out      | Serial      | Pi JST                                |            |                               | 2          |                 |                | GPIO13      | FSPIQ                |          |
| Reserved                | Reserved    | Pi JST                                |            |                               | 3          |                 |                | GPIO18      |                      |          |
| S0 White                | Output      | Motor Control DSub                    | DSUB15     |                               | 15         | Blue Stripe     |                | S0B+        |                      |          |
| S0 Black                | Output      | Motor Control DSub                    | DSUB15     |                               | 14         | Blue Solid      |                | S0A-        |                      |          |
| S0 Red                  | Output      | Motor Control DSub                    | DSUB15     |                               | 13         | Brown Stripe    |                | S0B-        |                      |          |
| S0 Green                | Output      | Motor Control DSub                    | DSUB15     |                               | 12         | Brown Solid     |                | S0A+        |                      |          |
| Pump White              | Output      | Motor Control DSub                    | DSUB15     |                               | 11         | Blue Stripe     |                | PumpB+      |                      |          |
| Pump Black              | Output      | Motor Control DSub                    | DSUB15     |                               | 10         | Blue Solid      |                | PumpA-      |                      |          |
| Pump Red                | Output      | Motor Control DSub                    | DSUB15     |                               | 6          | Brown Stripe    |                | PumpB-      |                      |          |
| Pump Green              | Output      | Motor Control DSub                    | DSUB15     |                               | 5          | Brown Solid     |                | PumpA+      |                      |          |
| S1 White                | Output      | Motor Control DSub                    | DSUB15     |                               | 4          | Blue Stripe     |                | S1B+        |                      |          |
| S1 Black                | Output      | Motor Control DSub                    | DSUB15     |                               | 3          | Blue Solid      |                | S1A-        |                      |          |
| S1 Red                  | Output      | Motor Control DSub                    | DSUB15     |                               | 2          | Brown Stripe    |                | S1B-        |                      |          |
| S1 Green                | Output      | Motor Control DSub                    | DSUB15     |                               | 1          | Brown Solid     |                | S1A+        |                      |          |
| Ground                  |             | Aux DSub (Warning, flipped connector) | DSUB2      | 4                             | 2          | Blue Solid      |                |             |                      |          |
| Ground                  |             | Aux DSub (Warning, flipped connector) | DSUB4      | 2                             | 4          | Green Solid     |                |             |                      |          |
| Ground                  |             | Aux DSub (Warning, flipped connector) | DSUB9      | 6                             | 9          | Orange Stripe   |                |             |                      |          |
| Reserved                |             | Aux DSub (Warning, flipped connector) | DSUB5      | 1                             | 5          |                 |                |             |                      |          |
| Reserved                |             | Aux DSub (Warning, flipped connector) | DSUB6      | 9                             | 6          |                 |                |             |                      |          |
| 5V Servo Power          | Output      | Aux DSub (Warning, flipped connector) | DSUB8      | 7                             | 8          | Orange Solid    |                |             |                      |          |
