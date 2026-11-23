Created with ChatGPT by Andrew (11/23)

# Configuring a UART TX Pin in ModusToolbox

Use the following settings to configure a pin as the UART transmit (TX) output for `USIC0_CH0`.

## 1. Configure the UART Peripheral

1. Open the **Peripherals** section in the Device Configurator.
2. Select **USIC0_CH0 | UART-1.0**.
3. Set **TXD** to the pin you want to use.

   `TXD = {Selected Pin}`

## 2. Configure the Selected Pin

1. Open the **Pins** section.
2. Select the same pin assigned to **TXD**.
3. Configure its properties as follows:

| Property | Setting |
|---|---|
| Pin Direction | Input/Output |
| Output Mode | Push Pull |
| Initial Output Level | Low |
| Driver Strength | Don't Care |
| Digital Input | `<unassigned>` |
| Digital Output | `USIC0_CH0 dout0 (CYBSP_DEBUG_UART) [USED]` |
| Store Config in Flash | Checked |

## 3. Set the Baud Rate

Set the UART **Baud Rate** to **9600**.

## 4. Final Configuration Checklist

- [ ] Baud rate is set to **9600**.
- [ ] `USIC0_CH0 | UART-1.0` is selected.
- [ ] **TXD** is assigned to the selected pin.
- [ ] The selected pin has all the settings listed above.
- [ ] **Store Config in Flash** is checked.