#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>

// Configurações de Rede
const char* ssid = "Archenar";
const char* password = "PASS1234567890000";

// Webserver, Canvas e manipulador de Thread
WebServer server(80);
M5Canvas canvas(&M5.Lcd);
TaskHandle_t WebTask;

// Html para renderizar na chamada de http://ip
const char index_html[] PROGMEM = R"rawcanvas(
<!DOCTYPE html>
<html>
<head><title>MONITOR</title></head>
<body style="background:#000; color:#0ff; text-align:center; font-family:monospace;">
    <h3>WEB MONITOR</h3>
    <img id="tela" style="width:800px; image-rendering:pixelated; border:2px solid #0ff;">
    <script>
        const img = document.getElementById('tela');
        function update() {
            // Atualiza a imagem a cada 100ms (10 FPS)
            img.src = "/img?t=" + Date.now();
        }
        img.onload = () => setTimeout(update, 100);
        img.onerror = () => setTimeout(update, 1000);
        update();
    </script>
</body>
</html>
)rawcanvas";

// Manipulador de rota Converte os bytes do tipo SPRITE para bmp para poder enviar
void handleImg() {
    int w = 240, h = 135;
    uint32_t pixSize = w * h * 3;
    uint32_t fileSize = 54 + pixSize;
    
    uint16_t* lineBuf = (uint16_t*)malloc(w * sizeof(uint16_t));
    uint8_t* bmpLine = (uint8_t*)malloc(w * 3);

    if (lineBuf && bmpLine) {
        WiFiClient client = server.client();
        
        // Cabeçalho HTTP Manual (Evita erro de Multiple Content-Length)
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: image/bmp\r\n");
        client.printf("Content-Length: %u\r\n", fileSize);
        client.print("Access-Control-Allow-Origin: *\r\n");
        client.print("Connection: close\r\n\r\n");

        // Cabeçalho BMP (54 bytes)
        uint8_t header[54];
        memset(header, 0, 54);
        header[0] = 'B'; header[1] = 'M';
        *((uint32_t*)(header + 2)) = fileSize;
        *((uint32_t*)(header + 10)) = 54;
        *((uint32_t*)(header + 14)) = 40;
        *((int32_t*)(header + 18)) = w;
        *((int32_t*)(header + 22)) = -h; // Top-down
        *((uint16_t*)(header + 26)) = 1;
        *((uint16_t*)(header + 28)) = 24;
        client.write(header, 54);

        // Processamento Linha por Linha direto do Canvas
        for (int y = 0; y < h; y++) {
            canvas.readRect(0, y, w, 1, lineBuf); // LÊ DA RAM, NÃO DO LCD
            uint8_t* p = bmpLine;
            for (int x = 0; x < w; x++) {
                uint16_t pixel = lineBuf[x];
                pixel = (pixel >> 8) | (pixel << 8); // Swap Endianness
                *p++ = (pixel & 0x1F) << 3;          // Blue
                *p++ = ((pixel >> 5) & 0x3F) << 2;   // Green
                *p++ = ((pixel >> 11) & 0x1F) << 3;  // Red
            }
            client.write(bmpLine, w * 3);
        }
    } 
    if(lineBuf) free(lineBuf);
    if(bmpLine) free(bmpLine);
}

// Task assincrona do servidor para processar as requicicoes
void webServerTask(void * p) {
    for(;;) {
        server.handleClient();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setRotation(1);

    // Cria o Sprite na RAM
    canvas.createSprite(240, 135);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Lcd.print(".");
    }

    server.on("/", []() { server.send(200, "text/html", index_html); });
    server.on("/img", handleImg);
    server.begin();

    // Inicia o Servidor no Core 0
    xTaskCreatePinnedToCore(webServerTask, "WebTask", 8192, NULL, 1, &WebTask, 0);

    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.print("IP: ");
    M5.Lcd.println(WiFi.localIP()); 
    delay(3000);
}

void loop() {
    M5.update();
    
    // Todas os desenhos serao realizados em Canvas
    canvas.fillSprite(BLACK); 
    
    // Exemplo de Relógio Futurista
    static unsigned long t;
    t = millis() / 1000;
    canvas.setTextColor(CYAN);
    canvas.setTextSize(3);
    canvas.setCursor(45, 50);
    canvas.printf("%02lu:%02lu:%02lu", (t/3600)%24, (t/60)%60, t%60);
    
    // Barra de progresso (visual Cyberdeck)
    int bar = map(t % 60, 0, 59, 0, 220);
    canvas.fillRect(10, 100, 220, 5, DARKGREY);
    canvas.fillRect(10, 100, bar, 5, GREEN);

    // Envia o desenho para a tela física
    canvas.pushSprite(0, 0); 
    
    delay(100);
}
