# Screensaver Secuencial — Verde atrae / Rojo repele (C++17 + SDL2)

Proyecto de simulación 2D secuencial con dos cuerpos principales:
- **Verde (A)**: **atrae** a las partículas.
- **Rojo (B)**: **repele** a las partículas.
- N satélites que rebotan en paredes, son atraídos/repelidos, y al tocar un principal **salen disparados** con cooldown (gravedad parcial que aumenta gradualmente → efecto de freno y redirección).
- Columna a la izquierda con la **lista de los últimos 10 FPS**.

> Minimalista: solo usa **SDL2** y **SDL2_ttf** (para texto).  
> Plataforma objetivo: **Windows (MSYS2 MinGW x64)**. También compila en Linux con `libsdl2-dev` y `libsdl2-ttf-dev`.

---

## Requisitos

- **Windows con MSYS2 MinGW x64**
- Paquetes:
  ```bash
  pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 \ mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-pkg-config
### Compilación

```bash 
g++ -O2 -std=c++17 main.cpp -o screensaver $(pkg-config --cflags --libs sdl2 SDL2_ttf)
```    

### Ejecución
./screensaver 
