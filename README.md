<div align="center">
  <img src="./assets/vagaroute-logo.png" alt="Logo de VagaRoute AI" width="128">
  <h1>VagaChatVITA</h1>
  <p>Una interfaz de chat homebrew para PlayStation Vita.</p>
  <p>
    <code>PlayStation Vita</code>
    <code>C11</code>
    <code>VitaSDK</code>
    <code>libvita2d</code>
  </p>
</div>

<p align="center">
  <a href="#sobre-el-proyecto">Sobre el proyecto</a> ·
  <a href="#compilar">Compilar</a> ·
  <a href="#probar-en-la-vita">Probar en la Vita</a> ·
  <a href="#controles">Controles</a>
</p>

---

## Sobre el proyecto

**VagaChatVITA** es una aplicacion homebrew escrita en C que lleva la experiencia de VagaRoute AI a PlayStation Vita. La interfaz esta disenada para la pantalla de la consola, con navegacion por botones, soporte tactil y una apariencia oscura inspirada en un cliente de chat moderno.

Al iniciar, la aplicacion solicita el nombre del usuario y lo guarda localmente en la Vita. Despues abre una pantalla de chat con compositor, historial persistente, ajustes de endpoint, API key enmascarada y selector de modelos.

El chat utiliza la API `chat/completions` compatible con OpenAI, recibe respuestas por streaming y mantiene la interfaz activa mientras genera. La opcion `Verificar conexion` consulta `/models` en el endpoint configurado.

## Caracteristicas

- Pantalla inicial con teclado oficial de Vita para guardar el nombre del usuario.
- Chat OpenAI-compatible con respuestas progresivas por streaming.
- Historial local persistente de hasta cuatro conversaciones.
- Biblioteca de titulos instalados con nombre, `TITLE_ID` y caratula.
- Chats contextuales por titulo con cuatro sugerencias generadas por el modelo.
- Interfaz disponible en espanol, ingles e italiano, con selector durante el inicio y en Ajustes.
- Prompts del sistema localizados con contexto de PlayStation Vita y de titulos oficiales o homebrew.
- Historial de mensajes en formato vertical con desplazamiento por botones y stick analogico.
- Logs separados por sesion en `ux0:data/vagachatvita/logs` sin almacenar la API key.
- Panel de ajustes con endpoint URL, API key enmascarada y modelos disponibles.
- Configuracion web local desde Ajustes, con IP y puerto mostrados en la Vita.
- Primera configuracion web protegida por contrasena; la API key nunca se devuelve en la pagina.
- La API key se guarda en el archivo local en formato codificado y las configuraciones antiguas se migran al guardar.
- Verificacion de conexion contra el endpoint configurado.
- Navegacion con botones, pantalla tactil y teclado oficial de PlayStation Vita.
- Renderizado 2D acelerado por GPU mediante `libvita2d`.
- Tipografia Manrope incluida en el VPK con su licencia OFL.
- Logo PNG y SVG empaquetados dentro de la aplicacion.
- README incluido dentro del VPK para referencia desde VitaShell.
- El icono de LiveArea usa `assets/icon0.png`, generado a partir del logo de la interfaz.
- El fondo de LiveArea usa `fondo.png` al abrir la burbuja de la aplicacion.
- La plantilla LiveArea incluye `startup.png` y `template.xml` para completar el paquete oficial.
- Generacion de ejecutables `.self` y paquetes `.vpk` para VitaShell.
- Compilacion automatizada con GitHub Actions y la imagen oficial de VitaSDK.

## Requisitos

Para compilar en Windows necesitas:

| Herramienta | Version o nota |
| --- | --- |
| Git for Windows | Para clonar y trabajar con el repositorio |
| PowerShell | 5.1 o superior |
| CMake | 3.16 o superior |
| Ninja | Necesario para el script de compilacion |
| VitaSDK | Compilador, headers y librerias para Vita |
| `libvita2d` | Libreria de renderizado utilizada por la interfaz |
| `curl`, `openssl`, `zstd` | Cliente HTTPS con TLS moderno y sus dependencias |

Para probar el resultado necesitas una PS Vita o PS TV preparada para ejecutar homebrew y VitaShell. El metodo de exploit, el firmware y la configuracion de la consola quedan fuera de este proyecto.

VitaSDK no se incluye en el repositorio. Si `libvita2d` no esta instalado, ejecuta lo siguiente con el SDK activo:

```powershell
$env:VITASDK = "$HOME\vitasdk"
& "$env:VITASDK\bin\vdpm.exe" pacman -- --noconfirm --sync libvita2d curl openssl zstd
```

## Instalar VitaSDK

Abre PowerShell en la raiz del proyecto. Si la politica de ejecucion bloquea scripts, el cambio solo afecta a la sesion actual:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\setup-vitasdk.ps1 -PersistEnvironment
```

El script utiliza el bootstrap oficial, valida la descarga y, por defecto, instala VitaSDK en `$HOME\vitasdk`. La opcion `-PersistEnvironment` guarda `VITASDK` y la carpeta `bin` en las variables del usuario.

Si ya instalaste VitaSDK en otra ruta:

```powershell
. .\scripts\activate-vitasdk.ps1 -InstallDirectory "C:\ruta\a\vitasdk"
arm-vita-eabi-gcc --version
```

Tambien puedes proporcionar un bootstrap descargado previamente:

```powershell
.\scripts\setup-vitasdk.ps1 `
  -BootstrapArchive "$HOME\Downloads\vdpm-0.1.1-x86_64-w64-mingw32.tar.bz2" `
  -PersistEnvironment
```

## Compilar

Activa VitaSDK y ejecuta el script incluido:

```powershell
. .\scripts\activate-vitasdk.ps1
.\scripts\build.ps1 -Configuration Release
```

Tambien puedes usar CMake directamente:

```powershell
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Los artefactos se generan en `build/`:

| Archivo | Uso |
| --- | --- |
| `VagaChatVITA.vpk` | Paquete instalable desde VitaShell |
| `VagaChatVITA.self` | Ejecutable de Vita sin empaquetar |

Los artefactos de compilacion estan excluidos mediante `.gitignore`.

## Probar en la Vita

1. Copia `build/VagaChatVITA.vpk` a `ux0:data` mediante USB o FTP.
2. Abre VitaShell y selecciona el VPK para instalarlo.
3. Ejecuta `VagaRoute AI` desde el LiveArea.
4. Pulsa `START` para guardar el nombre y salir de la aplicacion.

La configuracion se guarda localmente en:

```text
ux0:data/VagaRouteAI/config.ini
```

Desde `Ajustes > Configuracion web`, la Vita muestra una direccion como `http://192.168.1.20:8080`. Abrela desde un PC o telefono conectado a la misma red Wi-Fi. La primera visita solicita crear una contrasena; las siguientes requieren esa contrasena para cambiar el endpoint o la API key. El servidor solo se ejecuta mientras esta abierta esa pantalla.

La pagina usa HTTP local, no HTTPS. No abras el puerto fuera de tu red de confianza y no introduzcas la API key desde una red publica.

Los logs de cada ejecucion se guardan en:

```text
ux0:data/vagachatvita/logs/session-<tick>.log
```

## Controles

<details>
<summary>Abrir la guia de controles</summary>

### Nombre

| Entrada | Accion |
| --- | --- |
| `ARRIBA` / `ABAJO` | Cambiar entre el campo de nombre y el boton |
| `CRUZ` | Abrir el teclado, aceptar o guardar |
| `IZQUIERDA` / `DERECHA` | Mover el cursor en el teclado oficial |
| `CIRCULO` | Cancelar el teclado |
| `TRIANGULO` | Borrar el ultimo caracter o limpiar el campo |
| `START` | Guardar el nombre y salir |
| Pantalla tactil | Tocar el campo para abrir el teclado |

### Chat

| Entrada | Accion |
| --- | --- |
| `ARRIBA` / `ABAJO` | Cambiar entre el compositor y `Enviar` |
| `CRUZ` en el compositor | Abrir el teclado oficial |
| `CRUZ` en `Enviar` | Enviar el mensaje al modelo seleccionado |
| `SELECT` | Abrir el menu lateral |
| `CIRCULO` | Cancelar el teclado o cerrar el historial |
| `TRIANGULO` | Borrar el ultimo caracter del mensaje |
| `START` | Guardar el nombre y salir |
| Pantalla tactil | Tocar el compositor, `Enviar` o el reloj |
| `CIRCULO` durante una respuesta | Cancelar la generacion |
| `L` / `R` | Desplazar el historial hacia arriba o abajo |
| `IZQUIERDA` / `DERECHA` | Desplazamiento corto del historial |
| Stick analogico vertical | Desplazamiento progresivo del historial |

### Ajustes

| Entrada | Accion |
| --- | --- |
| `SELECT` o engranaje | Abrir los ajustes |
| `ARRIBA` / `ABAJO` | Moverse entre los campos |
| `CRUZ` | Editar el campo seleccionado o verificar la conexion |
| Pantalla tactil | Editar nombre, endpoint, API key o abrir una opcion |
| `CIRCULO` | Volver al chat |

El boton `Configuracion web` abre una pantalla con la direccion local. En esa pantalla, `CIRCULO` o `CRUZ` en `Salir` detienen el servidor y regresan a Ajustes.

En una conversacion nueva, `ARRIBA` / `ABAJO` recorren el campo de mensaje, enviar, modelo y preguntas rapidas. `IZQUIERDA` / `DERECHA` seleccionan una pregunta rapida y `CRUZ` la envia.

</details>

## Estructura del proyecto

```text
.
├── fondo.png
├── assets/
│   ├── cacert.pem
│   ├── icon0.png
│   ├── Manrope.ttf
│   ├── OFL-Manrope.txt
│   ├── startup.png
│   ├── template.xml
│   ├── vagaroute-logo.png
│   └── vagaroute-logo.svg
├── scripts/
│   ├── activate-vitasdk.ps1
│   ├── build.ps1
│   └── setup-vitasdk.ps1
├── src/
│   ├── chat.c
│   ├── chat.h
│   ├── i18n.c
│   ├── i18n.h
│   ├── main.c
│   ├── web_config.c
│   └── web_config.h
├── .github/workflows/build.yml
├── CMakeLists.txt
└── README.md
```

## Empezar a desarrollar

- Modifica la logica principal en `src/main.c`.
- Cambia `VITA_APP_NAME`, `VITA_TITLEID` y `VITA_VERSION` en `CMakeLists.txt`.
- Usa un `VITA_TITLEID` unico de exactamente nueve caracteres ASCII en mayusculas.
- Instala librerias adicionales con `vdpm` desde una terminal con VitaSDK activo.
- Agrega nuevos recursos al VPK mediante entradas `FILE` en `vita_create_vpk`.

El workflow de GitHub Actions configura `libvita2d`, ejecuta CMake y publica el `.vpk` como artefacto de cada compilacion.

## WSL2

VitaSDK recomienda WSL2 para Windows. Dentro de Ubuntu instala `git`, `cmake` y las herramientas basicas, instala VitaSDK con `bootstrap-vitasdk.sh`, define `VITASDK` y ejecuta los mismos comandos de CMake del apartado de compilacion.

## Solucion de problemas

- **`VITASDK is not defined`:** ejecuta `. .\scripts\activate-vitasdk.ps1` en la misma terminal donde compilas.
- **`CMake is required`:** instala CMake y abre una terminal nueva para actualizar `PATH`.
- **Faltan librerias:** instala los paquetes con `vdpm pacman -- --noconfirm --sync libvita2d curl openssl zstd`.
- **Error de `TITLE_ID`:** usa exactamente nueve caracteres ASCII en mayusculas.
- **La Vita no muestra la aplicacion:** confirma que VitaShell instalo el `.vpk` completo y que la consola puede ejecutar homebrew.

## Licencias y recursos

- La fuente Manrope se distribuye junto a su licencia en `assets/OFL-Manrope.txt`.
- El paquete incluye el bundle de autoridades certificadoras de Mozilla publicado por curl para validar HTTPS.
- VitaSDK, `libvita2d` y las demas librerias de Vita son dependencias externas con sus propias licencias.
- El logo utilizado en este README y dentro del VPK es `assets/vagaroute-logo.png`.
