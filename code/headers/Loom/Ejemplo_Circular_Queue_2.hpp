// Cola acotada de varios productores y varios consumidores (MPMC), C++20, un solo header.
//
// El algoritmo es de Dmitry Vyukov (1024cores.net, "Bounded MPMC queue"): cada hueco del
// anillo lleva un número de secuencia atómico que indica de quién es el turno (productor
// o consumidor) y en qué vuelta. Esta es una implementación propia, no una copia de su código.
//
// Uso:
//   mpmc_queue<int> q(1000);          // capacidad real: 1024 (potencia de 2, mínimo 2)
//   if (!q.try_push(42)) { /* llena */ }
//   int v; if (q.try_pop(v)) { /* v == 42 */ }
//
// Contrato:
//  - Todas las operaciones son seguras entre hilos, no bloquean y devuelven false si la
//    cola está llena (push) o vacía (pop). Cuesta un CAS por operación; no asigna memoria.
//  - T no necesita constructor por defecto ni ser copiable (vale std::unique_ptr), pero
//    construir, copiar/mover y asignar por movimiento debe ser noexcept (se exige con
//    `requires`). Motivo: una excepción tras reclamar el hueco lo dejaría sin publicar.
//  - No es lock-free en sentido estricto: un hilo suspendido entre reclamar un hueco y
//    publicarlo retiene ese hueco, y try_pop puede devolver false aunque haya elementos
//    en huecos posteriores. Para esperar, haga spin/yield o añada su propia espera.
//  - El destructor debe ejecutarse sin operaciones en curso; destruye lo que quede dentro.
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <class T>
class mpmc_queue
{
    static constexpr std::size_t cache_line = 64;

    struct cell
    {
        std::atomic<std::size_t> seq;
        alignas(T) std::byte storage[sizeof(T)];

        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    };

public:
    explicit mpmc_queue(std::size_t min_capacity)
        : capacity_(round_capacity(min_capacity)),
          mask_(capacity_ - 1),
          cells_(new cell[capacity_]) {
        for (std::size_t i = 0; i < capacity_; ++i)
            cells_[i].seq.store(i, std::memory_order_relaxed);
    }

    // Debe invocarse sin operaciones concurrentes en curso (como cualquier destructor).
    ~mpmc_queue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            const std::size_t end = enq_pos_.load(std::memory_order_relaxed);
            for (std::size_t pos = deq_pos_.load(std::memory_order_relaxed); pos != end; ++pos)
                cells_[pos & mask_].ptr()->~T();
        }
    }

    mpmc_queue(const mpmc_queue&) = delete;
    mpmc_queue& operator=(const mpmc_queue&) = delete;

    std::size_t capacity() const noexcept { return capacity_; }

    // Si el constructor de T lanzara después de reclamar el hueco, éste quedaría sin
    // publicar y bloquearía a los consumidores para siempre. Como no se puede deshacer
    // el CAS, se exige en tiempo de compilación que construir/mover/copiar no lance.
    [[nodiscard]] bool try_push(const T& value)
        requires std::is_nothrow_copy_constructible_v<T>
    {
        return try_emplace(value);
    }

    [[nodiscard]] bool try_push(T&& value)
        requires std::is_nothrow_move_constructible_v<T>
    {
        return try_emplace(std::move(value));
    }

    // Si devuelve false (cola llena) no se ha construido nada ni se ha tocado ningún argumento.
    template <class... Args>
    [[nodiscard]] bool try_emplace(Args&&... args)
        requires std::is_nothrow_constructible_v<T, Args...>
    {
        cell* c;
        std::size_t pos = enq_pos_.load(std::memory_order_relaxed);
        for (;;) {
            c = &cells_[pos & mask_];
            const std::size_t seq = c->seq.load(std::memory_order_acquire);
            const auto dif = static_cast<std::ptrdiff_t>(seq - pos);
            if (dif == 0) {
                // Hueco libre para esta posición: lo reclamo. Si pierdo la
                // carrera, el CAS deja en `pos` el valor actual y reintento.
                if (enq_pos_.compare_exchange_weak(pos, pos + 1,std::memory_order_relaxed))
                    break;
            } else if (dif < 0) {
                return false;  // el hueco aún tiene el dato de la vuelta anterior: llena
            } else {
                pos = enq_pos_.load(std::memory_order_relaxed);  // `pos` obsoleto
            }
        }
        ::new (static_cast<void*>(c->storage)) T(std::forward<Args>(args)...);
        c->seq.store(pos + 1, std::memory_order_release);  // publica el dato
        return true;
    }

    [[nodiscard]] bool try_pop(T& out)
        requires std::is_nothrow_move_assignable_v<T>
    {
        cell* c;
        std::size_t pos = deq_pos_.load(std::memory_order_relaxed);
        for (;;) {
            c = &cells_[pos & mask_];
            const std::size_t seq = c->seq.load(std::memory_order_acquire);
            const auto dif = static_cast<std::ptrdiff_t>(seq - (pos + 1));
            if (dif == 0) {
                if (deq_pos_.compare_exchange_weak(pos, pos + 1,
                                                   std::memory_order_relaxed))
                    break;
            } else if (dif < 0) {
                return false;  // nadie ha publicado aquí todavía: vacía
            } else {
                pos = deq_pos_.load(std::memory_order_relaxed);
            }
        }
        out = std::move(*c->ptr());
        c->ptr()->~T();
        c->seq.store(pos + capacity_, std::memory_order_release);  // libre en la próxima vuelta
        return true;
    }

private:
    // Potencia de 2 >= 2. Con capacidad 1, `pos + 1` y `pos + capacity` coinciden y
    // "listo para leer" no se distingue de "libre". El tope garantiza que
    // `seq - pos` cabe en un entero con signo y que `bit_ceil` no desborda.
    static std::size_t round_capacity(std::size_t requested) {
        constexpr std::size_t max_capacity = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 2);
        if (requested > max_capacity) throw std::length_error("mpmc_queue: capacidad demasiado grande");
        return std::bit_ceil(std::max<std::size_t>(requested, 2));
    }

    // Solo lectura tras construir: comparten línea de caché entre sí.
    alignas(cache_line) const std::size_t capacity_;
    const std::size_t mask_;
    const std::unique_ptr<cell[]> cells_;
    // Cada contador caliente, en su propia línea.
    alignas(cache_line) std::atomic<std::size_t> enq_pos_{0};
    alignas(cache_line) std::atomic<std::size_t> deq_pos_{0};
};
