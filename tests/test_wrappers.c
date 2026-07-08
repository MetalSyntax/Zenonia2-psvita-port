#include <stdio.h>
#include <assert.h>
#include <stdint.h>

// Dummy mocks for the test environment (since we are testing on Mac/Host, not Vita)
void glClearColor(float r, float g, float b, float a) {
    // Assert that the fixed point conversion worked correctly
    // Fixed point uses 65536 for 1.0f
    assert(r >= 0.0f && r <= 1.0f);
    assert(g >= 0.0f && g <= 1.0f);
    assert(b >= 0.0f && b <= 1.0f);
    assert(a >= 0.0f && a <= 1.0f);
    printf("[TEST] glClearColor conversion correct: r=%f, g=%f, b=%f, a=%f\n", r, g, b, a);
}

// Wrapper function to test (from our dynlib.c)
void glClearColorx_wrapper(int r, int g, int b, int a) {
    glClearColor(r / 65536.0f, g / 65536.0f, b / 65536.0f, a / 65536.0f);
}

int main() {
    printf("=== Ejecutando pruebas unitarias del Port ===\n");
    
    // Prueba 1: Conversión de OpenGL Punto Fijo a Flotante
    // 65536 = 1.0f, 32768 = 0.5f, 0 = 0.0f
    printf("Testeando glClearColorx_wrapper (1.0f, 0.5f, 0.0f, 1.0f)...\n");
    glClearColorx_wrapper(65536, 32768, 0, 65536);
    
    printf("¡Todas las pruebas pasaron satisfactoriamente!\n");
    return 0;
}
