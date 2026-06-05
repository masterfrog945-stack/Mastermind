  #include "NRF24L01.h"
  #include <stdint.h>
  #include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "NRF24L01_Define.h"

  
  
  /* Global definitions and variables */
  #define NRF24_CE_PIN    GPIO_NUM_17
  #define NRF24_CSN_PIN   GPIO_NUM_10
  #define NRF24_SCK_PIN   GPIO_NUM_35
  #define NRF24_MOSI_PIN  GPIO_NUM_0
  #define NRF24_MISO_PIN  GPIO_NUM_37

  #define SPI_HOST        SPI2_HOST    // Use the HSPI-compatible SPI2 host
  #define SPI_CLK_SPEED   1000000      // 1 MHz SPI clock

  static spi_device_handle_t spi_handle;
  /* Transmit section */
  uint8_t NRF24L01_TxAddress[5] = {0x11, 0x22, 0x33, 0x44, 0x55};		// 5-byte transmit address
  #define NRF24L01_TX_PACKET_WIDTH		32							// Transmit payload width, valid range: 1 to 32 bytes
  uint8_t NRF24L01_TxPacket[NRF24L01_TX_PACKET_WIDTH];				// Transmit payload buffer
  
  /* Receive section */
  uint8_t NRF24L01_RxAddress[5] = {0x01, 0x02, 0x03, 0x04, 0x05};		// 5-byte receive address for pipe 0
  #define NRF24L01_RX_PACKET_WIDTH		32							// Receive payload width for pipe 0, valid range: 1 to 32 bytes
  uint8_t NRF24L01_RxPacket[NRF24L01_RX_PACKET_WIDTH];				// Receive payload buffer
  
  void NRF24L01_W_CE(uint8_t BitValue)
  {
      /* Drive the CE pin high or low according to BitValue. */
      gpio_set_level(NRF24_CE_PIN, BitValue);
  }

  void NRF24L01_W_CSN(uint8_t BitValue)
  {
      /* Drive the CSN pin high or low according to BitValue. */
      gpio_set_level(NRF24_CSN_PIN, BitValue);
  }

  void NRF24L01_W_SCK(uint8_t BitValue)
  {
      /* Drive the SCK pin high or low according to BitValue. */
      gpio_set_level(NRF24_SCK_PIN, BitValue);
  }

  void NRF24L01_W_MOSI(uint8_t BitValue)
  {
      /* Drive the MOSI pin high or low according to BitValue. */
      gpio_set_level(NRF24_MOSI_PIN, BitValue);
  }
  
  uint8_t NRF24L01_R_MISO(void)
  {
      /* Read and return the current logic level on the MISO pin. */
      return gpio_get_level(NRF24_MISO_PIN);
  }

  void NRF24L01_Init(void)
{
	/* Initialize the nRF24L01 by configuring its main registers. */
	/* Both transmitter and receiver must use matching settings for communication. */
	NRF24L01_WriteReg(NRF24L01_CONFIG, 0x08);		// CONFIG register: IRQs enabled, CRC enabled, 1-byte CRC, power down, TX mode
	NRF24L01_WriteReg(NRF24L01_EN_AA, 0x3F);		// Enable auto acknowledgment on RX pipes 0 to 5
	NRF24L01_WriteReg(NRF24L01_EN_RXADDR, 0x01);	// Enable only receive pipe 0
	NRF24L01_WriteReg(NRF24L01_SETUP_AW, 0x03);		// Set address width to 5 bytes
	NRF24L01_WriteReg(NRF24L01_SETUP_RETR, 0x03);	// Set auto retransmit: 250 us delay, 3 retries
	NRF24L01_WriteReg(NRF24L01_RF_CH, 0x72);		// RF channel: 2400 MHz + channel 0x72
	NRF24L01_WriteReg(NRF24L01_RF_SETUP, 0x0E);		// RF setup: 2 Mbps data rate, 0 dBm output power
	
	/* Set the payload width of receive pipe 0. */
	NRF24L01_WriteReg(NRF24L01_RX_PW_P0, NRF24L01_RX_PACKET_WIDTH);
	
	/* Set the 5-byte address of receive pipe 0. */
	NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_RxAddress, 5);
	
	/* Clear all data from the TX FIFO. */
	NRF24L01_FlushTx();
	
	/* Clear all data from the RX FIFO. */
	NRF24L01_FlushRx();
	
	/* Clear MAX_RT, TX_DS, and RX_DR by writing 1 to the corresponding STATUS bits. */
	NRF24L01_WriteReg(NRF24L01_STATUS, 0x70);
	
	/* After initialization, switch the device to receive mode by default. */
	NRF24L01_Rx();
}
  
  void NRF24L01_GPIO_Init(void)
  {
     gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << NRF24_CE_PIN) |  // CE pin
                        (1ULL << NRF24_CSN_PIN) | // CSN pin
                        (1ULL << GPIO_NUM_5), 
        .mode = GPIO_MODE_OUTPUT,                 // Output mode
        .pull_up_en = GPIO_PULLUP_DISABLE,        // Disable internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,    // Disable internal pull-down
        .intr_type = GPIO_INTR_DISABLE,           // Disable GPIO interrupt
    };
    gpio_config(&output_config);
    
    /* Configure MISO as an input pin with internal pull-up enabled. */
    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << NRF24_MISO_PIN), // MISO pin
        .mode = GPIO_MODE_INPUT,                  // Input mode
        .pull_up_en = GPIO_PULLUP_ENABLE,         // Enable internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,    // Disable internal pull-down
        .intr_type = GPIO_INTR_DISABLE,           // Disable GPIO interrupt
    };
    gpio_config(&input_config);
    
    /* Set the default pin levels after GPIO initialization. */
    NRF24L01_W_CE(0);     // CE low: leave active TX/RX mode
    NRF24L01_W_CSN(1);    // CSN high: deselect the SPI device
    
    printf("NRF24L01 GPIO initial\n");
  }

  void NRF24L01_SPI_Init(void)
  {
    if (spi_handle) {
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
    }
    spi_bus_free(SPI2_HOST);

    spi_bus_config_t buscfg={
      .mosi_io_num=NRF24_MOSI_PIN,
      .miso_io_num=NRF24_MISO_PIN,
      .sclk_io_num=NRF24_SCK_PIN,
      .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
        .intr_flags = 0
    };

    spi_device_interface_config_t devcfg={
      .clock_speed_hz=SPI_CLK_SPEED,
      .mode=0,
      .spics_io_num=-1,
      .queue_size=1,
      .pre_cb = NULL,
    .post_cb = NULL
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));
    printf("SPI初始化完成\n");
  }

  uint8_t NRF24L01_ReadReg(uint8_t reg)
{
    uint8_t value;
    
    NRF24L01_W_CSN(0);                    // Select the SPI device
    SPI_ExchangeByte(NRF24L01_R_REGISTER | reg);                // Send the register address or command byte
    value = SPI_ExchangeByte(NRF24L01_NOP);       // Read the register value
    NRF24L01_W_CSN(1);                    // Deselect the SPI device
    
    return value;
}

void NRF24L01_ReadRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count)
{
	uint8_t i;
	
	/* Pull CSN low to start an SPI transaction. */
	NRF24L01_W_CSN(0);
	
	/* Send the read-register command. The lower 5 bits contain the register address. */
	SPI_ExchangeByte(NRF24L01_R_REGISTER | RegAddress);

	/* After the read command, clock out multiple bytes from the specified register address. */
	for (i = 0; i < Count; i ++)
	{
		/* Store the received byte into the output buffer. */
		DataArray[i] = SPI_ExchangeByte(NRF24L01_NOP);
	}
	
	/* Pull CSN high to end the SPI transaction. */
	NRF24L01_W_CSN(1);
}


void NRF24L01_FlushRx(){
    NRF24L01_W_CSN(0);
    SPI_ExchangeByte(NRF24L01_FLUSH_RX);  // Flush the RX FIFO
    NRF24L01_W_CSN(1);
}

void NRF24L01_FlushTx(){
    NRF24L01_W_CSN(0);
    SPI_ExchangeByte(NRF24L01_FLUSH_TX);  // Flush the TX FIFO
    NRF24L01_W_CSN(1);
}

void NRF24L01_WriteReg(uint8_t reg, uint8_t value)
{
    NRF24L01_W_CSN(0);                    // Select the SPI device
    SPI_ExchangeByte(0x20 | reg);         // Send the write command together with the register address
    SPI_ExchangeByte(value);              // Write the register value
    NRF24L01_W_CSN(1);                    // Deselect the SPI device
}

void NRF24L01_WriteRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count)
{
	uint8_t i;
	
	/* Pull CSN low to start an SPI transaction. */
	NRF24L01_W_CSN(0);
	
	/* Send the write-register command. The lower 5 bits contain the register address. */
	SPI_ExchangeByte(NRF24L01_W_REGISTER | RegAddress);
	
	/* After the write command, send multiple bytes to the specified register address. */
	for (i = 0; i < Count; i ++)
	{
		/* Write one byte from the input buffer. */
		SPI_ExchangeByte(DataArray[i]);
	}
	
	/* Pull CSN high to end the SPI transaction. */
	NRF24L01_W_CSN(1);
}

void NRF24L01_ReadBuf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    
    NRF24L01_W_CSN(0);                    // Select the SPI device
    SPI_ExchangeByte(reg);                // Send the register address or command byte
    
    for (i = 0; i < len; i++) {
        buf[i] = SPI_ExchangeByte(0xFF);  // Read one data byte
    }
    
    NRF24L01_W_CSN(1);                    // Deselect the SPI device
}

void NRF24L01_WriteBuf(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    
    NRF24L01_W_CSN(0);                    // Select the SPI device
    SPI_ExchangeByte(0x20 | reg);         // Send the write command together with the register address
    
    for (i = 0; i < len; i++) {
        SPI_ExchangeByte(buf[i]);         // Write one data byte
    }
    
    NRF24L01_W_CSN(1);                    // Deselect the SPI device
}

void NRF24L01_WriteTxPayload(uint8_t *DataArray, uint8_t Count)
{
    NRF24L01_W_CSN(0);
    
    SPI_ExchangeByte(NRF24L01_W_TX_PAYLOAD);
    
    for (int i = 0; i < Count; i++) {
        SPI_ExchangeByte(DataArray[i]);
    }
    
    NRF24L01_W_CSN(1);
}

void NRF24L01_ReadRxPayload(uint8_t *DataArray, uint8_t Count)
{
	/* Pull CSN low to start an SPI transaction. */
	NRF24L01_W_CSN(0);
	SPI_ExchangeByte(NRF24L01_R_RX_PAYLOAD);
	for (uint8_t i = 0; i < Count; i ++)
	{
		DataArray[i] = SPI_ExchangeByte(NRF24L01_NOP);
	}
	
	/* Pull CSN high to end the SPI transaction. */
	NRF24L01_W_CSN(1);
}

uint8_t SPI_ExchangeByte(uint8_t data)
{
    spi_transaction_t t;
    uint8_t rx_data;
    
    memset(&t, 0, sizeof(t));
    t.length = 8;                    // 8-bit transfer
    t.tx_buffer = &data;             // Transmit buffer
    t.rx_buffer = &rx_data;          // Receive buffer
    
    // Execute the SPI transfer
    spi_device_transmit(spi_handle, &t);
    
    return rx_data;
}

void SPI_ExchangeBuffer(uint8_t *tx_data, uint8_t *rx_data, uint8_t len)
{
    spi_transaction_t t;
    
    memset(&t, 0, sizeof(t));
    t.length = len * 8;              // Total number of bits
    t.tx_buffer = tx_data;           // Transmit buffer
    t.rx_buffer = rx_data;           // Receive buffer
    
    // Execute the SPI transfer
    spi_device_transmit(spi_handle, &t);
}

void NRF24L01_PowerDown(void){
    uint8_t Config;

    NRF24L01_W_CE(0);

    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    Config &= ~0x02;
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config); // Read-modify-write the CONFIG register
}

void NRF24L01_StandbyI(void){
    uint8_t Config;

    NRF24L01_W_CE(0);

    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    Config |= ~0x02;
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config);
}

void NRF24L01_Rx(void)
{
    uint8_t config;

    NRF24L01_W_CE(0);

    config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    
    config |= 0x03;  // Set PRIM_RX and PWR_UP to enter powered receive mode
    
    NRF24L01_WriteReg(NRF24L01_CONFIG, config);

    NRF24L01_W_CE(1);
    
    //printf("nRF24L01 entered receive mode\n");
}

void NRF24L01_Tx(void)
{
    uint8_t config;

    NRF24L01_W_CE(0);

    config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    
    config |= 0x02;   // Set PWR_UP to power up the device
    config &= ~0x01;  // Clear PRIM_RX to select transmit mode
    
    NRF24L01_WriteReg(NRF24L01_CONFIG, config);

    NRF24L01_W_CE(1);
    
    printf("nRF24L01进入发送模式\n");
}

uint8_t NRF24L01_ReadStatus(void)
{
	uint8_t Status;
	
	/* Pull CSN low to start an SPI transaction. */
	NRF24L01_W_CSN(0);

	/* Send the NOP command as the first byte of the SPI transaction. */
	/* The STATUS register is returned during the first SPI byte exchange. */
	Status = SPI_ExchangeByte(NRF24L01_NOP);
	
	/* Pull CSN high to end the SPI transaction. */
	NRF24L01_W_CSN(1);
	
	/* Return the value of the STATUS register. */
	return Status;
}

uint8_t NRF24L01_Send(void){
    uint8_t Status;
	uint8_t SendFlag;
	uint32_t Timeout;
	
	/* Set the 5-byte transmit address from NRF24L01_TxAddress. */
	NRF24L01_WriteRegs(NRF24L01_TX_ADDR, NRF24L01_TxAddress, 5);
	
	/* Write the transmit payload from NRF24L01_TxPacket. */
	NRF24L01_WriteTxPayload(NRF24L01_TxPacket, NRF24L01_TX_PACKET_WIDTH);

    /* Set RX pipe 0 to the transmit address so that ACK packets can be received. */
	NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_TxAddress, 5);
	
	/* Enter transmit mode after the address and payload have been written. */
	NRF24L01_Tx();
	
	/* Set the timeout counter based on how many STATUS polling attempts are allowed. */
	Timeout = 10000;
	
	/* Poll the STATUS register until the transmission completes or times out. */
	while (1)
	{
		/* Read the STATUS register and store it in Status. */
		Status = NRF24L01_ReadStatus();
		
		/* Decrease the timeout counter. */
		Timeout --;
		if (Timeout == 0)			// Timeout counter reached zero
		{
			SendFlag = 4;			// Transmission timed out
			// NRF24L01_Init();      // Optional recovery: reinitialize the device after a TX error
			break;					// Exit the polling loop
		}
		
		/* Check the transmission result according to the STATUS register. */
		if ((Status & 0x30) == 0x30)		// Both MAX_RT and TX_DS are set, which is an invalid TX status
		{
			SendFlag = 3;			// Invalid STATUS value
			// NRF24L01_Init();      // Optional recovery: reinitialize the device after a TX error
			break;					// Exit the polling loop
		}
		else if ((Status & 0x10) == 0x10)	// MAX_RT is set
		{
			SendFlag = 2;			// Maximum retransmission count reached without receiving ACK
			// NRF24L01_Init();      // Optional recovery: reinitialize the device after a TX error
			break;					// Exit the polling loop
		}
		else if ((Status & 0x20) == 0x20)	// TX_DS is set
		{
			SendFlag = 1;			// Transmission completed successfully
            printf("success!");
			break;					// Exit the polling loop
		}
	}
	
	/* Clear MAX_RT and TX_DS by writing 1 to the corresponding STATUS bits. */
	NRF24L01_WriteReg(NRF24L01_STATUS, 0x30);
	
	/* Clear all data from the TX FIFO. */
	NRF24L01_FlushTx();
	
	/* Restore the original address of receive pipe 0 after transmission. */
	/* This step can be skipped if the TX address and RX pipe 0 address are the same. */
	NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_RxAddress, 5);
	
	/* Return the device to receive mode after transmission. */
	NRF24L01_Rx();
		
	/* Return the transmission result flag. */
	return SendFlag;

}

uint8_t NRF24L01_Receive(void)
{
	uint8_t Status, Config;
	uint8_t ReceiveFlag;
	
	/* Read the STATUS register and store it in Status. */
	Status = NRF24L01_ReadStatus();
	
	/* Read the CONFIG register and store it in Config. */
	Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
	
	/* Determine the receive status according to CONFIG and STATUS. */
	if ((Config & 0x02) == 0x00)		// PWR_UP is 0, so the device is still in power-down mode
	{
		ReceiveFlag = 3;				// Device is still in power-down mode
		NRF24L01_Init();				// Reinitialize the device to recover from a receive error
	}
	else if ((Status & 0x30) == 0x30)	// Both MAX_RT and TX_DS are set, which is an invalid TX status
	{
		ReceiveFlag = 2;				// Invalid STATUS value
		NRF24L01_Init();				// Reinitialize the device to recover from a receive error
	}
	else if ((Status & 0x40) == 0x40)	// RX_DR is set
	{
		ReceiveFlag = 1;				// New data has been received
		
		/* Read the received payload into NRF24L01_RxPacket. */
		NRF24L01_ReadRxPayload(NRF24L01_RxPacket, NRF24L01_RX_PACKET_WIDTH);
		
		/* Clear RX_DR by writing 1 to the corresponding STATUS bit. */
		NRF24L01_WriteReg(NRF24L01_STATUS, 0x40);

		/* Clear all data from the RX FIFO. */
		NRF24L01_FlushRx();
	}
	else
	{
		ReceiveFlag = 0;				// No new data has been received
	}
	
	/* Return the receive result flag. */
	return ReceiveFlag;
}