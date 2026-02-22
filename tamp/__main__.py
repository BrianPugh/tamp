try:
    from .cli.main import run_app
except ImportError:
    import sys

    print(
        "The tamp CLI requires additional dependencies. Install them with:\n  pip install tamp[cli]",
        file=sys.stderr,
    )
    raise SystemExit(1)

run_app()
