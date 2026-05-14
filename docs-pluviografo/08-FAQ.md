# FAQ

Q: ¿Dónde coloco mis credenciales?
A: Copia `config_env.example.h` a `config_env.h` y edita los `SECRET_*`. El archivo está gitignored.

Q: ¿Cómo restauro la hora si no hay red?
A: El firmware usa `RTClib` y el DS3231 como fallback cuando la red no responde.

Q: ¿Puedo cambiar el intervalo de reporte?
A: Sí, edita `TIEMPO_DORMIR_SEG` en `PluviografoC.ino` o define el valor en `config_env.h` si prefieres.
