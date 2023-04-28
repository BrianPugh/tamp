import heapq
from typing import Any, Dict, Union


class Node:
    def __init__(self, char, prob):
        self.char = char
        self.prob = prob
        self.left = None
        self.right = None

    def __lt__(self, other):
        return self.prob < other.prob


def create_priority_queue(probs):
    queue = []
    if not isinstance(probs, dict):
        probs = dict(zip(range(len(probs)), probs))

    for token, prob in probs.items():
        heapq.heappush(queue, Node(token, prob))
    return queue


def build_huffman_tree(queue):
    while len(queue) > 1:
        left = heapq.heappop(queue)
        right = heapq.heappop(queue)
        internal_node = Node(None, left.prob + right.prob)
        internal_node.left = left
        internal_node.right = right
        heapq.heappush(queue, internal_node)
    return heapq.heappop(queue)


def generate_huffman_codes(node, code=""):
    if node is None:
        return {}
    if node.char is not None:
        return {node.char: code}
    codes = {}
    codes.update(generate_huffman_codes(node.left, code + "0"))
    codes.update(generate_huffman_codes(node.right, code + "1"))
    return codes


def huffman_coding(probs: Union[list, Dict[Any, float]]) -> Dict[int, str]:
    priority_queue = create_priority_queue(probs)
    huffman_tree_root = build_huffman_tree(priority_queue)
    huffman_codes = generate_huffman_codes(huffman_tree_root)
    return huffman_codes
