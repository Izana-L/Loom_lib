// spsc_queue.hpp
//
// Cola circular SPSC (un productor, un consumidor) sin bloqueos. C++20.
//
//  * Capacidad fija en tiempo de compilación, potencia de dos: el módulo se
//    resuelve con una máscara (index & (Capacity - 1)).
//  * wait-free: try_push / try_pop terminan en un número acotado de pasos,
//    sin locks, sin CAS y sin bucles de reintento.
//  * Contrato: como mucho UN hilo llama a try_push / try_emplace (productor)
//    y como mucho UN hilo llama a try_pop / try_consume (consumidor).
//
// Diseño
// ------
//  * head_ (lo escribe solo el consumidor) y tail_ (lo escribe solo el
//    productor) son contadores que crecen sin parar; NO se les aplica la
//    máscara al guardarlos. El hueco se calcula como (contador & mask).
//      vacía: tail == head          llena: tail - head == Capacity
//    Así se aprovechan los Capacity huecos (no hace falta dejar uno libre).
//    El desbordamiento de size_t es inocuo: la aritmética sin signo es módulo
//    2^N y Capacity divide a 2^N, así que las diferencias siguen siendo válidas.
//
//  * Orden de memoria (la pareja release/acquire es lo único que hace falta):
//      productor: construye el dato      -> tail_.store(release)
//      consumidor: tail_.load(acquire)   -> lee el dato
//      consumidor: lee y destruye el dato -> head_.store(release)
//      productor: head_.load(acquire)    -> reutiliza el hueco
//    Cada hilo lee su propio índice con relaxed (nadie más lo modifica).
//
//  * Cada hilo guarda una copia en caché del índice del otro (cached_head_ /
//    cached_tail_) y solo vuelve a leer el atómico remoto cuando la copia
//    indica "llena" / "vacía". Así, en régimen normal, se evita que la línea
//    de caché del otro hilo rebote en cada operación.
//
//  * head_/cached_tail_, tail_/cached_head_ y el almacenamiento viven en líneas
//    de caché distintas para evitar false sharing.

#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace spsc {

// Valor fijo a propósito: std::hardware_destructive_interference_size cambia
// entre compiladores/flags y GCC avisa si se usa en un header. 64 B vale para
// x86-64 y la mayoría de ARM; en Apple Silicon (128 B) súbelo si lo necesitas.
inline constexpr std::size_t cache_line_size = 64;

template <typename T, std::size_t Capacity>
class Queue {
    static_assert(std::has_single_bit(Capacity),
                  "Capacity debe ser una potencia de dos (1, 2, 4, 8, ...)");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "T debe tener destructor noexcept");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "std::atomic<size_t> debe ser lock-free en esta plataforma");

public:
    using value_type = T;

    Queue() = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // Destruye los elementos que queden. Requiere que ya no haya otros hilos
    // usando la cola.
    ~Queue()
    {
        while (try_consume([](T&) {})) {
        }
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    // ---------------------------------------------------------------- productor

    // Construye el elemento directamente en la cola. Devuelve false si está llena.
    // Si el constructor de T lanza, la cola no cambia.
    template <typename... Args>
    requires std::constructible_from<T, Args...>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - cached_head_ == Capacity) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail - cached_head_ == Capacity) {
                return false;  // llena
            }
        }
        ::new (raw_slot(tail)) T(std::forward<Args>(args)...);
        tail_.store(tail + 1, std::memory_order_release);  // publica el dato
        return true;
    }

    [[nodiscard]] bool try_push(const T& value) noexcept(
        std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return try_emplace(value);
    }

    [[nodiscard]] bool try_push(T&& value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires std::move_constructible<T>
    {
        return try_emplace(std::move(value));
    }

    // ---------------------------------------------------------------- consumidor

    // Llama a f(T&) con el elemento más antiguo y, si f termina sin lanzar,
    // lo destruye y libera el hueco. Devuelve false si está vacía.
    // Permite consumir sin mover/copiar y funciona con tipos no movibles.
    template <std::invocable<T&> F>
    bool try_consume(F&& f) noexcept(std::is_nothrow_invocable_v<F, T&>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) {
                return false;  // vacía
            }
        }
        T* p = slot(head);
        std::forward<F>(f)(*p);
        std::destroy_at(p);
        head_.store(head + 1, std::memory_order_release);  // libera el hueco
        return true;
    }

    // Saca el elemento más antiguo en `out`. Devuelve false si está vacía.
    [[nodiscard]] bool try_pop(T& out)
        requires std::is_move_assignable_v<T>
    {
        return try_consume([&out](T& v) { out = std::move(v); });
    }

    // Variante que devuelve un optional (vacío si la cola está vacía).
    [[nodiscard]] std::optional<T> try_pop()
        requires std::move_constructible<T>
    {
        std::optional<T> result;
        try_consume([&result](T& v) { result.emplace(std::move(v)); });
        return result;
    }

    // -------------------------------------------------------------- observadores

    // Tamaño aproximado: con el otro hilo activo puede estar desfasado en
    // cuanto se devuelve. Útil para métricas, no para tomar decisiones.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        // head primero: head <= tail en todo momento y ambos solo crecen,
        // así que tail(leído después) - head(leído antes) nunca es negativo.
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t n = tail - head;
        return n > Capacity ? Capacity : n;
    }

private:
    static constexpr std::size_t mask = Capacity - 1;
    static constexpr std::size_t storage_align = alignof(T) > cache_line_size ? alignof(T) : cache_line_size;

    void* raw_slot(std::size_t index) noexcept {
        return storage_ + (index & mask) * sizeof(T);
    }
    T* slot(std::size_t index) noexcept {
        return std::launder(static_cast<T*>(raw_slot(index)));
    }

    // Línea del productor
    alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
    std::size_t cached_head_{0};  // copia (posiblemente antigua) de head_

    // Línea del consumidor
    alignas(cache_line_size) std::atomic<std::size_t> head_{0};
    std::size_t cached_tail_{0};  // copia (posiblemente antigua) de tail_

    // Almacenamiento sin inicializar; los T se construyen/destruyen a mano.
    alignas(storage_align) std::byte storage_[Capacity * sizeof(T)];
};

}  // namespace spsc
