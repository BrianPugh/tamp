"""
CLI script to generate Huffman codes with Zipf's law distributed frequencies.
"""

import heapq
import math
from pathlib import Path
from typing import Annotated, Dict, Optional

from cyclopts import App, Parameter


class HuffmanNode:
    """Node for Huffman tree construction."""

    def __init__(self, char: Optional[int], freq: float, left=None, right=None):
        self.char = char
        self.freq = freq
        self.left = left
        self.right = right

    def __lt__(self, other):
        return self.freq < other.freq


class HuffmanEncoder:
    """Huffman encoder with Zipf's law distributed frequencies."""

    def __init__(self, s: float = 1.0):
        """
        Initialize encoder with Zipf's law parameters.

        Args:
            s: Zipf parameter (typically around 1.0 for natural language)
               Higher values = more skewed distribution
        """
        self.s = s

    def generate_zipf_frequencies(self, n_values: int = 32) -> Dict[int, float]:
        """Generate frequencies following Zipf's law."""
        frequencies = {}

        # Calculate the normalization constant (generalized harmonic number)
        harmonic_sum = sum(1.0 / (rank**self.s) for rank in range(1, n_values + 1))

        for i in range(n_values):
            # Value i gets rank (i+1) in Zipf's law
            rank = i + 1
            # Zipf's law: frequency = 1 / (rank^s * harmonic_sum)
            freq = (1.0 / (rank**self.s)) / harmonic_sum
            frequencies[i] = freq

        return frequencies

    def build_huffman_tree(self, frequencies: Dict[int, float]) -> HuffmanNode:
        """Build Huffman tree from frequencies."""
        # Create leaf nodes and add to priority queue
        heap = []
        for char, freq in frequencies.items():
            node = HuffmanNode(char, freq)
            heapq.heappush(heap, node)

        # Build tree by combining nodes
        while len(heap) > 1:
            left = heapq.heappop(heap)
            right = heapq.heappop(heap)

            merged = HuffmanNode(None, left.freq + right.freq, left, right)
            heapq.heappush(heap, merged)

        return heap[0] if heap else None

    def extract_codes(self, root: HuffmanNode) -> Dict[int, tuple[str, int]]:
        """Extract Huffman codes from tree as (code, num_bits) tuples."""
        if not root:
            return {}

        codes = {}

        def dfs(node, code=""):
            if node.char is not None:  # Leaf node
                final_code = code if code else "0"  # Handle single node case
                codes[node.char] = (final_code, len(final_code))
                return

            if node.left:
                dfs(node.left, code + "0")
            if node.right:
                dfs(node.right, code + "1")

        dfs(root)
        return codes

    def generate_huffman_codes(self, n_values: int = 32) -> Dict[int, tuple[str, int]]:
        """Generate Huffman codes with Zipf's law distributed frequencies."""
        frequencies = self.generate_zipf_frequencies(n_values)
        root = self.build_huffman_tree(frequencies)
        return self.extract_codes(root)

    def calculate_efficiency_metrics(
        self, frequencies: Dict[int, float], codes: Dict[int, tuple[str, int]]
    ) -> Dict[str, float]:
        """Calculate Huffman coding efficiency metrics."""
        # Calculate entropy H(X) = -sum(p_i * log2(p_i))
        entropy = 0.0
        for freq in frequencies.values():
            if freq > 0:
                entropy -= freq * math.log2(freq)

        # Calculate average code length L_avg = sum(p_i * l_i)
        avg_length = 0.0
        for value, freq in frequencies.items():
            if value in codes:
                _, code_length = codes[value]
                avg_length += freq * code_length

        # Calculate metrics
        efficiency = entropy / avg_length if avg_length > 0 else 0.0
        redundancy = avg_length - entropy

        return {"entropy": entropy, "avg_length": avg_length, "efficiency": efficiency, "redundancy": redundancy}


app = App(help="Generate Huffman codes with Zipf-distributed frequencies", default_parameter=Parameter(negative=""))


@app.default
def generate_codes(
    *,
    skew: Annotated[float, Parameter(alias="-s")] = 1.0,
    values: int = 64,
    show_frequencies: bool = False,
    invert: bool = False,
    output: Path = Path("huffman.py"),
) -> None:
    """
    Generate Huffman codes for values 0-(values-1) with Zipf-distributed frequencies.

    Args:
        skew: Zipf distribution parameter (higher = more skewed).
        values: Number of values to encode (0 to values-1).
        show_frequencies: Whether to display the generated frequencies.
        invert: Whether to invert all Huffman codes (0->1, 1->0).
        output: Output file to save the Huffman table (default: huffman.py).
    """
    if values <= 0:
        print("Error: Number of values must be positive")
        return

    if skew <= 0:
        print("Error: s parameter must be positive")
        return

    encoder = HuffmanEncoder(s=skew)
    frequencies = encoder.generate_zipf_frequencies(values)

    if show_frequencies:
        print("Generated Zipf frequencies:")
        for i in range(min(10, values)):  # Show first 10
            print(f"  {i}: {frequencies[i]:.6f}")
        if values > 10:
            print(f"  ... (showing first 10 of {values})")
        print()

    codes = encoder.generate_huffman_codes(values)

    # Apply inversion if requested
    if invert:
        inverted_codes = {}
        for value, (code, num_bits) in codes.items():
            inverted_code = code.translate(str.maketrans("01", "10"))
            inverted_codes[value] = (inverted_code, num_bits)
        codes = inverted_codes

    # Calculate efficiency metrics
    metrics = encoder.calculate_efficiency_metrics(frequencies, codes)

    # Find longest code length
    max_length = max(num_bits for _, num_bits in codes.values()) if codes else 0

    print("Huffman Coding Efficiency Analysis:")
    print(f"  Entropy (theoretical minimum): {metrics['entropy']:.4f} bits/symbol")
    print(f"  Average code length:           {metrics['avg_length']:.4f} bits/symbol")
    print(f"  Coding efficiency:             {metrics['efficiency']:.4f} ({metrics['efficiency']*100:.2f}%)")
    print(f"  Redundancy (quantization loss): {metrics['redundancy']:.4f} bits/symbol")
    print(f"  Longest code:                  {max_length} bits")
    print()

    # Convert binary strings to 0b notation and create tuples
    result = {}
    for value, (code, num_bits) in codes.items():
        # Convert binary string to integer, then to 0b notation
        int(code, 2) if code else 0
        result[value] = (f"0b{code}", num_bits)

    # Create the output content
    output_lines = []
    output_lines.append(f"# Huffman codes generated with Zipf parameter s={skew}")
    output_lines.append("_EXTENDED_HUFFMAN_TABLE = {")
    for i in range(values):
        if i in result:
            code, num_bits = result[i]
            comma = "," if i < values - 1 else ""
            output_lines.append(f"    {i}: ({code}, {num_bits}){comma}")
    output_lines.append("}")

    # Print to console
    print("Huffman codes dictionary:")
    for line in output_lines:
        print(line)

    # Write to file
    try:
        output.write_text("\n".join(output_lines) + "\n")
        print(f"\nHuffman table saved to: {output}")
    except Exception as e:
        print(f"\nError writing to file {output}: {e}")


if __name__ == "__main__":
    app()
