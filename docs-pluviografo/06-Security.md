# Security & Secrets

Buenas prácticas
- Nunca commitear archivos con credenciales (`config_env.h`). Usa `config_env.example.h` como plantilla.
- Mantén `config_env.h` fuera del control de versiones (`.gitignore` debería contener la ruta).

Opciones seguras
- Cargar secretos en tiempo de despliegue desde CI/CD (variables de entorno) y generar `config_env.h` en el artefacto de build.
- Usar almacenamiento cifrado en dispositivo (LittleFS + AES) si necesitas proteger secretos en campo.

Eliminar secretos de historial
- Si se publicaron secretos, purgar el historial: usar `git filter-repo` (recomendado) o `bfg`.
- Después de purgar, forzar push y avisar a colaboradores que deben re-clonar.

Contacto
- Si encuentras secretos en este repo, contácta al responsable y rota las credenciales inmediatamente.
