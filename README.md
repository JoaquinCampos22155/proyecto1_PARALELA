# Screensaver — Verde atrae / Rojo repele (C++17 + SDL2)

Proyecto de simulación 2D secuencial y paralelo con dos cuerpos principales:
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
Para ambos se debe de ubicar en la respectiva carpeta

Secuencial:
```bash 
g++ -O2 -std=c++17 main_sec.cpp -o screensaver $(pkg-config --cflags --libs sdl2 SDL2_ttf)
```    

Paralelo:
```bash 
g++ -O2 -std=c++17 -Wall -Wextra -Wshadow main_par.cpp -o screensaver $(pkg-config --cflags --libs sdl2 SDL2_ttf) -fopenmp
```

### Ejecución
Secuencial:
./screensaver 

Paralelo (el número de hilos se puede cambiar):
OMP_NUM_THREADS=8 ./screensaver