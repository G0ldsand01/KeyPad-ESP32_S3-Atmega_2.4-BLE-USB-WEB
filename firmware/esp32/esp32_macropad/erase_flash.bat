@echo off
echo Effacement de la flash ESP32-S3...
echo.
echo Assurez-vous que l'ESP32-S3 est connecte et que le port est correct.
echo.
pause

esptool.py --chip esp32s3 --port COM3 erase_flash

echo.
echo Flash effacee! Vous pouvez maintenant televerser le code.
pause
