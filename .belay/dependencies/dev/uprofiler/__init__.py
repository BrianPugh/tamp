from time import ticks_diff, ticks_us  # type: ignore[reportGeneralTypeIssues]

_t_import = ticks_us()

_BOLD = "\033[1m"
_RESET = "\033[0m"

# Default print period
print_period = 1


def _ticks_delta(t_start):
    return ticks_diff(ticks_us(), t_start)


class _Counter:
    registry = {}

    def __init__(self, name, print_period):
        self.name = name
        self.print_period = print_period
        self.n = 0
        self.t_time_us = 0

        self.registry[name] = self

    def record(self, delta):
        self.n += 1
        self.t_time_us += delta

    @property
    def average(self):
        return self.t_time_us / self.n

    def __str__(self):
        t_time_ms = self.t_time_us / 1000
        return f"{self.name: 24.24} {self.n : >8} calls {t_time_ms:>12.3f}ms total {t_time_ms/self.n:>12.3f}ms average"

    def print(self):
        pp = self.print_period
        if pp is None:
            pp = print_period

        if pp > 0 and self.n % pp == 0:
            print(self)


# Can't use class; micropython will have issues decorating methods then.
def profile(f=None, *, name=None, print_period=None):
    """Function/Method decorator."""
    if f is None:
        # decorated with arguments
        return lambda x: profile(x, name=name, print_period=print_period)

    if name is None:
        # TODO: I think micropython 1.20.0 will have __qualname__
        name = f.__name__

    try:
        counter = _Counter.registry[name]
    except KeyError:
        counter = _Counter(name, print_period)

    def inner(*args, **kwargs):
        t_start = ticks_us()
        result = f(*args, **kwargs)
        delta = _ticks_delta(t_start)
        counter.record(delta)
        counter.print()
        return result

    return inner


def _table_formatter(name, calls, total_pct, total_ms, avg_ms):
    return f"{name: 32.32} {calls: >8} {total_pct: >10} {total_ms: >13} {avg_ms: >13}"


def print_results():
    """Print summary.

    To be called at end of script.
    """
    t_total_ms = ticks_diff(ticks_us(), _t_import) / 1000

    print()
    print(f"{_BOLD}Total-Time:{_RESET} {t_total_ms:6.3f}ms")

    header = _table_formatter(
        "Name", "Calls", "Total (%)", "Total (ms)", "Average (ms)"
    )

    print(_BOLD + header + _RESET)
    print("-" * len(header))

    counters = _Counter.registry.values()
    counters = sorted(counters, key=lambda x: x.t_time_us, reverse=True)
    for counter in counters:
        t_counter_total_ms = counter.t_time_us / 1000
        print(
            _table_formatter(
                name=counter.name,
                calls=counter.n,
                total_pct=round(100 * t_counter_total_ms / t_total_ms, 2),
                total_ms=t_counter_total_ms,
                avg_ms=t_counter_total_ms / counter.n if counter.n else 0,
            )
        )
