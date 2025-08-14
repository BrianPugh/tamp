"""
CLI script to optimize extended Huffman table parameters for tamp compression.

This script optimizes the `skew` and `values` parameters from huffman_zipf.py
to minimize the average compression ratio across test datasets.
"""

from pathlib import Path
from typing import Annotated, Dict, Tuple

from cyclopts import App, Parameter

# Add the project root to the path so we can import tamp
from huffman_zipf import HuffmanEncoder
from scipy.optimize import differential_evolution, minimize

import tamp
import tamp.compressor

app = App(help="Optimize extended Huffman table parameters for tamp compression")


def generate_huffman_table(skew: float, values: int) -> Dict[int, Tuple[int, int]]:
    """Generate Huffman table using the same logic as huffman_zipf.py."""
    encoder = HuffmanEncoder(s=skew)
    codes = encoder.generate_huffman_codes(values)

    # Convert to the format expected by tamp (integer codes instead of binary strings)
    result = {}
    for value, (code_str, num_bits) in codes.items():
        code_int = int(code_str, 2) if code_str else 0
        result[value] = (code_int, num_bits)

    return result


def check_max_code_length(huffman_table: Dict[int, Tuple[int, int]]) -> int:
    """Check the maximum code length in the Huffman table."""
    if not huffman_table:
        return 0
    return max(num_bits for _, num_bits in huffman_table.values())


def compress_file(file_path: Path, huffman_table: Dict[int, Tuple[int, int]]) -> float:
    """Compress a file using the given Huffman table and return compression ratio."""
    try:
        # Temporarily replace the global Huffman table
        original_table = tamp.compressor._EXTENDED_HUFFMAN_TABLE
        tamp.compressor._EXTENDED_HUFFMAN_TABLE = huffman_table
        data = file_path.read_bytes()
        if len(data) == 0:
            return float("inf")  # Invalid file
        compressed_data = tamp.compressor.compress(data)
        ratio = len(compressed_data) / len(data)
        return ratio

    except Exception as e:
        print(f"Error compressing {file_path}: {e}")
        return float("inf")  # Return a bad ratio on error

    finally:
        # Restore the original table
        tamp.compressor._EXTENDED_HUFFMAN_TABLE = original_table


def objective_function(params: Tuple[float, int], test_files: list[Path]) -> float:
    """Objective function to minimize: average compression ratio across test files."""
    skew, values = params

    # Handle NaN values
    import math

    if math.isnan(skew) or math.isnan(values):
        return float("inf")

    # Ensure values is within valid range and is an integer
    values = max(4, min(128, int(round(values))))

    # Generate Huffman table
    try:
        huffman_table = generate_huffman_table(skew, values)
    except Exception as e:
        print(f"Error generating Huffman table with skew={skew}, values={values}: {e}")
        return float("inf")

    # Check constraint: maximum code length must be <= 8
    max_length = check_max_code_length(huffman_table)
    if max_length > 8:
        return float("inf")  # Invalid solution

    # Calculate average compression ratio across test files
    ratios = []
    for file_path in test_files:
        ratio = compress_file(file_path, huffman_table)
        if ratio == float("inf"):
            return float("inf")  # Invalid solution
        ratios.append(ratio)

    avg_ratio = sum(ratios) / len(ratios)
    print(f"skew={skew:.3f}, values={values}, max_length={max_length}, avg_ratio={avg_ratio:.4f}")

    return avg_ratio


@app.default
def optimize(
    *,
    skew_min: float = 0.1,
    skew_max: float = 5.0,
    values_min: int = 4,
    values_max: int = 128,
    method: str = "differential_evolution",
    maxiter: int = 50,
    grid_search: Annotated[bool, Parameter(negative="")] = False,
    grid_points: int = 10,
    output: Path = Path("optimized_huffman.py"),
) -> None:
    """
    Optimize extended Huffman table parameters for tamp compression.

    This function finds the optimal skew and values parameters that minimize
    the average compression ratio across the test datasets.

    Parameters
    ----------
    skew_min : float, default=0.1
        Minimum skew value
    skew_max : float, default=5.0
        Maximum skew value
    values_min : int, default=4
        Minimum values count
    values_max : int, default=128
        Maximum values count
    method : str, default="differential_evolution"
        Optimization method
    maxiter : int, default=50
        Maximum iterations
    grid_search : bool, default=False
        Use grid search instead of optimization
    grid_points : int, default=10
        Number of grid points per dimension for grid search
    output : Path, default=Path("optimized_huffman.py")
        Output file for optimized table
    """
    # Define test files
    build_dir = Path(__file__).parent.parent / "build"
    test_files = [
        build_dir / "enwik8",
        build_dir / "RPI_PICO-20250415-v1.25.0.uf2",
    ]

    # Add Silesia corpus files
    silesia_dir = build_dir / "silesia"
    if silesia_dir.exists():
        for silesia_file in silesia_dir.iterdir():
            if silesia_file.is_file():
                test_files.append(silesia_file)

    # Check that test files exist
    valid_files = []
    for file_path in test_files:
        if file_path.exists() and file_path.is_file():
            valid_files.append(file_path)
            print(f"Using test file: {file_path}")
        else:
            print(f"Warning: Test file not found: {file_path}")

    if not valid_files:
        print("Error: No valid test files found!")
        return

    test_files = valid_files

    print(f"\nOptimizing over {len(test_files)} test files...")
    print(f"Skew range: [{skew_min}, {skew_max}]")
    print(f"Values range: [{values_min}, {values_max}]")
    print(f"Method: {method}")
    print(f"Max iterations: {maxiter}")
    print()

    # Initial guess (current values based on the existing table)
    initial_skew = 1.059  # Current value from the comment in compressor.py
    initial_values = len(tamp.compressor._EXTENDED_HUFFMAN_TABLE)  # Current table size

    # Ensure initial guess is within bounds
    initial_skew = max(skew_min, min(skew_max, initial_skew))
    initial_values = max(values_min, min(values_max, initial_values))

    x0 = [initial_skew, initial_values]
    bounds = [(skew_min, skew_max), (values_min, values_max)]

    print(f"Initial guess: skew={initial_skew}, values={initial_values}")

    # Define the objective function wrapper
    def obj_wrapper(x):
        return objective_function((x[0], x[1]), test_files)

    # Perform optimization or grid search
    if grid_search:
        print("Starting grid search...")
        import numpy as np

        # Create grid points
        skew_points = np.linspace(skew_min, skew_max, grid_points)
        values_points = np.arange(values_min, values_max + 1, dtype=int)

        best_result = float("inf")
        best_params = None
        all_results = []

        total_evaluations = len(skew_points) * len(values_points)
        evaluation_count = 0

        print(f"Evaluating {total_evaluations} parameter combinations...")

        for skew in skew_points:
            for values in values_points:
                evaluation_count += 1
                result_val = obj_wrapper([skew, values])
                all_results.append((skew, values, result_val))

                if result_val < best_result:
                    best_result = result_val
                    best_params = (skew, values)

                if evaluation_count % 10 == 0:
                    print(
                        f"Progress: {evaluation_count}/{total_evaluations} ({evaluation_count / total_evaluations * 100:.1f}%)"
                    )

        # Create a result object similar to scipy.optimize results
        class GridSearchResult:
            def __init__(self, x, fun, success=True, message="Grid search completed"):
                self.x = x
                self.fun = fun
                self.success = success
                self.message = message
                self.nfev = total_evaluations

        result = GridSearchResult(best_params, best_result)

        # Show top 5 results
        all_results.sort(key=lambda x: x[2])
        print("\nTop 5 parameter combinations:")
        for i, (s, v, r) in enumerate(all_results[:5]):
            print(f"{i + 1}. skew={s:.3f}, values={int(v)}, ratio={r:.4f}")

    else:
        print("Starting optimization...")
        if method == "differential_evolution":
            # Use differential evolution for global optimization
            result = differential_evolution(
                obj_wrapper,
                bounds,
                maxiter=maxiter,
                seed=42,  # For reproducible results
                disp=True,  # Show progress
                integrality=[False, True],  # skew=continuous, values=integer
            )
        else:
            # Use traditional optimization methods
            result = minimize(obj_wrapper, x0, method=method, bounds=bounds, options={"maxiter": maxiter})

    print("\nOptimization completed!")
    print(f"Success: {result.success}")
    print(f"Message: {result.message}")
    print(f"Function evaluations: {result.nfev}")

    optimal_skew, optimal_values = result.x
    optimal_values = int(round(optimal_values))

    print("\nOptimal parameters:")
    print(f"  skew: {optimal_skew:.6f}")
    print(f"  values: {optimal_values}")
    print(f"  Final objective value: {result.fun:.6f}")

    # Generate the optimal Huffman table
    print("\nGenerating optimal Huffman table...")
    try:
        optimal_table = generate_huffman_table(optimal_skew, optimal_values)
        max_length = check_max_code_length(optimal_table)

        print("Optimal table generated successfully:")
        print(f"  Table size: {len(optimal_table)}")
        print(f"  Maximum code length: {max_length} bits")

        if max_length > 8:
            print(f"Warning: Maximum code length ({max_length}) exceeds 8 bits!")

        # Save the optimized table to file
        output_lines = []
        output_lines.append(f"# Optimized Huffman codes with skew={optimal_skew:.6f}, values={optimal_values}")
        output_lines.append("# Generated by optimize-extended-huffman.py")
        output_lines.append(f"# Objective value: {result.fun:.6f}")
        output_lines.append("_EXTENDED_HUFFMAN_TABLE = {")

        for i in range(optimal_values):
            if i in optimal_table:
                code, num_bits = optimal_table[i]
                # Convert to binary representation for readability
                code_bin = f"0b{bin(code)[2:].zfill(num_bits)}"
                comma = "," if i < optimal_values - 1 else ""
                output_lines.append(f"    {i}: ({code_bin}, {num_bits}){comma}")

        output_lines.append("}")

        # Write to file
        output.write_text("\n".join(output_lines) + "\n")
        print(f"Optimized table saved to: {output}")

    except Exception as e:
        print(f"Error generating optimal table: {e}")


if __name__ == "__main__":
    app()
