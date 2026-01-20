// Import styles
import '../css/styles.css';

// Import assets to ensure they're bundled
import logoSvg from '../../assets/logo-compressed.svg';

// Import Tamp module
import { compress, decompress, initialize, initializeDictionary } from '../../wasm/dist/index.mjs';

// Global state
let currentOperation = 'compress';
let selectedFiles = [];
let processedData = null;
let wasmInitialized = false;

// DOM elements (initialized in DOMContentLoaded)
let dropZone,
  fileInput,
  fileInfo,
  actionBtn,
  loading,
  results,
  compressionOptions,
  textMode,
  windowBitsSelect,
  lazyMatchingCheckbox,
  textWindowBitsSelect,
  textLazyMatchingCheckbox,
  plainTextArea,
  compressedTextArea,
  textStats,
  textStatsTitle,
  textStatsContent,
  progressBarFill,
  progressText,
  customDictionaryTextArea,
  dictionaryError,
  toggleDictionaryBtn,
  customDictionarySection;

// Initialize WASM module eagerly on page load
let wasmInitPromise = null;
async function initializeWasm() {
  if (wasmInitPromise) return wasmInitPromise;

  wasmInitPromise = initialize()
    .then(() => {
      wasmInitialized = true;
      // Update UI when WASM is ready
      if (actionBtn && actionBtn.textContent === 'Loading...') {
        actionBtn.textContent = currentOperation === 'compress' ? 'Compress' : 'Decompress';
        actionBtn.disabled = false;
      }
    })
    .catch(() => {
      wasmInitialized = false;
      if (actionBtn) {
        actionBtn.textContent = 'Error loading';
        actionBtn.disabled = true;
      }
    });

  return wasmInitPromise;
}

// Start preloading immediately
initializeWasm();

// Initialize DOM elements and event listeners after DOM is ready
document.addEventListener('DOMContentLoaded', () => {
  // Set logo source
  const logoImg = document.querySelector('img[alt="Tamp Logo"]');
  if (logoImg) {
    logoImg.src = logoSvg;
  }

  // Query DOM elements
  dropZone = document.getElementById('dropZone');
  fileInput = document.getElementById('fileInput');
  fileInfo = document.getElementById('fileInfo');
  actionBtn = document.getElementById('actionBtn');
  loading = document.getElementById('loading');
  results = document.getElementById('results');
  compressionOptions = document.getElementById('compressionOptions');
  textMode = document.getElementById('textMode');
  windowBitsSelect = document.getElementById('windowBits');
  lazyMatchingCheckbox = document.getElementById('lazyMatching');
  textWindowBitsSelect = document.getElementById('textWindowBits');
  textLazyMatchingCheckbox = document.getElementById('textLazyMatching');
  plainTextArea = document.getElementById('plainText');
  compressedTextArea = document.getElementById('compressedText');
  textStats = document.getElementById('textStats');
  textStatsTitle = document.getElementById('textStatsTitle');
  textStatsContent = document.getElementById('textStatsContent');
  progressBarFill = document.getElementById('progressBarFill');
  progressText = document.getElementById('progressText');
  customDictionaryTextArea = document.getElementById('customDictionary');
  dictionaryError = document.getElementById('dictionaryError');
  toggleDictionaryBtn = document.getElementById('toggleDictionaryBtn');
  customDictionarySection = document.getElementById('customDictionarySection');

  // Show loading indicator until WASM is ready
  if (actionBtn && !wasmInitialized) {
    actionBtn.textContent = 'Loading...';
    actionBtn.disabled = true;
  }

  // Add event listeners for toggle buttons
  document.getElementById('compressToggle').addEventListener('click', () => setOperation('compress'));
  document.getElementById('decompressToggle').addEventListener('click', () => setOperation('decompress'));
  document.getElementById('textToggle').addEventListener('click', () => setOperation('text'));

  // Add event listeners for action buttons
  actionBtn.addEventListener('click', processFiles);
  document.getElementById('downloadBtn').addEventListener('click', downloadResult);
  document.getElementById('compressTextBtn').addEventListener('click', compressTextContent);
  document.getElementById('decompressTextBtn').addEventListener('click', decompressTextContent);
  toggleDictionaryBtn.addEventListener('click', toggleCustomDictionary);

  // Drag and drop event handlers
  dropZone.addEventListener('dragover', e => {
    e.preventDefault();
    dropZone.classList.add('drag-over');
  });

  dropZone.addEventListener('dragleave', e => {
    e.preventDefault();
    if (!dropZone.contains(e.relatedTarget)) {
      dropZone.classList.remove('drag-over');
    }
  });

  dropZone.addEventListener('drop', e => {
    e.preventDefault();
    dropZone.classList.remove('drag-over');
    handleFileSelection(Array.from(e.dataTransfer.files));
  });

  // File input handler
  fileInput.addEventListener('change', e => {
    handleFileSelection(Array.from(e.target.files));
  });

  // Browse button handler
  document.querySelector('.browse-btn').addEventListener('click', () => {
    fileInput.click();
  });

  // Dictionary validation on input change
  customDictionaryTextArea.addEventListener('input', () => {
    if (!customDictionarySection.classList.contains('hidden')) {
      const windowBits = parseInt(textWindowBitsSelect.value);
      const validation = validateCustomDictionary(customDictionaryTextArea.value, windowBits);
      if (!validation.isValid) {
        showDictionaryError(validation.error);
      } else {
        hideDictionaryError();
      }
    }
  });

  // Validate dictionary when window size changes
  textWindowBitsSelect.addEventListener('change', () => {
    if (!customDictionarySection.classList.contains('hidden')) {
      const windowBits = parseInt(textWindowBitsSelect.value);
      const validation = validateCustomDictionary(customDictionaryTextArea.value, windowBits);
      if (!validation.isValid) {
        showDictionaryError(validation.error);
      } else {
        hideDictionaryError();
      }
    }
  });
});

function setOperation(operation) {
  currentOperation = operation;

  // Update toggle buttons
  document.getElementById('compressToggle').classList.toggle('active', operation === 'compress');
  document.getElementById('decompressToggle').classList.toggle('active', operation === 'decompress');
  document.getElementById('textToggle').classList.toggle('active', operation === 'text');

  if (operation === 'text') {
    // Show text mode, hide file mode
    textMode.classList.add('show');
    dropZone.style.display = 'none';
    actionBtn.style.display = 'none';
    compressionOptions.classList.remove('show');
    compressionOptions.classList.add('hidden');
    fileInfo.classList.remove('show');
    hideResults();
  } else {
    // Show file mode, hide text mode
    textMode.classList.remove('show');
    dropZone.style.display = 'block';
    actionBtn.style.display = 'block';
    hideTextStats();

    // Update UI text
    const isCompress = operation === 'compress';
    document.getElementById('dropText').textContent = isCompress ? 'Drop files here' : 'Drop .tamp files here';
    actionBtn.textContent = isCompress ? 'Compress' : 'Decompress';

    // Show/hide compression options
    if (isCompress) {
      compressionOptions.classList.remove('hidden');
      compressionOptions.classList.add('show');
    } else {
      compressionOptions.classList.add('hidden');
      compressionOptions.classList.remove('show');
    }

    // Reset state
    selectedFiles = [];
    processedData = null;
    hideResults();
    updateFileInfo();
  }
}

function updateFileInfo() {
  if (selectedFiles.length === 0) {
    fileInfo.classList.remove('show');
    actionBtn.disabled = true;
    return;
  }

  const totalSize = selectedFiles.reduce((sum, file) => sum + file.size, 0);
  const fileText = selectedFiles.length === 1 ? selectedFiles[0].name : `${selectedFiles.length} files selected`;

  document.getElementById('fileName').textContent = fileText;
  document.getElementById('fileSize').textContent = `Total size: ${formatBytes(totalSize)}`;

  fileInfo.classList.add('show');
  actionBtn.disabled = false;
}

function formatBytes(bytes) {
  if (bytes === 0) return '0 bytes';
  return bytes.toLocaleString() + ' bytes';
}

function showLoading() {
  loading.classList.remove('hidden');
  loading.classList.add('show');
  actionBtn.disabled = true;
  resetProgress();
}

function hideLoading() {
  loading.classList.remove('show');
  loading.classList.add('hidden');
  actionBtn.disabled = false;
}

/**
 * Reset progress bar to 0%
 */
function resetProgress() {
  progressBarFill.style.width = '0%';
  progressText.textContent = '0%';
}

/**
 * Update progress bar with given percentage
 * @param {number} percentage - Progress percentage (0-100)
 */
function updateProgress(percentage) {
  const clampedPercentage = Math.min(100, Math.max(0, percentage));

  progressBarFill.style.width = `${clampedPercentage}%`;
  progressText.textContent = `${Math.round(clampedPercentage)}%`;

  // Yield to the event loop to allow DOM updates to render
  return new Promise(resolve => {
    requestAnimationFrame(() => {
      requestAnimationFrame(resolve);
    });
  });
}

function showResults(title, stats, isError = false) {
  document.getElementById('resultTitle').textContent = title;

  const statsContainer = document.getElementById('resultStats');
  statsContainer.innerHTML = '';

  stats.forEach(stat => {
    const statElement = document.createElement('div');
    statElement.className = 'stat';
    statElement.innerHTML = `
            <span>${stat.label}</span>
            <span>${stat.value}</span>
        `;
    statsContainer.appendChild(statElement);
  });

  results.classList.toggle('error', isError);
  results.classList.add('show');
}

function hideResults() {
  results.classList.remove('show', 'error');
}

async function processFiles() {
  if (selectedFiles.length === 0) return;

  showLoading();
  hideResults();

  // Ensure WASM is initialized before processing
  if (!wasmInitialized) {
    await wasmInitPromise;
  }

  try {
    const results = [];
    let totalOriginalSize = 0;
    let totalProcessedSize = 0;
    let totalProcessingTime = 0;

    // Calculate total size for overall progress
    const totalFileSize = selectedFiles.reduce((sum, file) => sum + file.size, 0);
    let processedFileSize = 0;

    for (let fileIndex = 0; fileIndex < selectedFiles.length; fileIndex++) {
      const file = selectedFiles[fileIndex];
      const arrayBuffer = await file.arrayBuffer();
      const data = new Uint8Array(arrayBuffer);

      let processedData;
      let newFileName;

      const startTime = performance.now();

      if (currentOperation === 'compress') {
        const windowBits = parseInt(windowBitsSelect.value);
        const options = {
          window: windowBits,
          // Add progress callback for compression with overall progress calculation
          onPoll: async progressInfo => {
            const bytesProcessed = progressInfo.bytesProcessed || 0;
            const totalBytes = progressInfo.totalBytes || 0;

            const fileProgress = totalBytes > 0 ? bytesProcessed / totalBytes : 0;
            const overallProgress =
              totalFileSize > 0 ? ((processedFileSize + fileProgress * file.size) / totalFileSize) * 100 : 0;
            await updateProgress(overallProgress);
          },
        };

        // Add lazy matching option if supported (requires TAMP_LAZY_MATCHING during compilation)
        if (lazyMatchingCheckbox.checked) {
          options.lazy_matching = true;
        }

        processedData = await compress(data, options);
        newFileName = file.name + '.tamp';
      } else {
        // For decompression, remove .tamp extension if present
        // Show basic progress for decompression (no callback support)
        const fileProgress = ((fileIndex + 1) / selectedFiles.length) * 100;
        updateProgress(fileProgress);

        processedData = await decompress(data);
        newFileName = file.name.endsWith('.tamp') ? file.name.slice(0, -5) : file.name + '.decompressed';
      }

      const endTime = performance.now();
      const processingTime = endTime - startTime;
      totalProcessingTime += processingTime;

      // Update processed file size for overall progress
      processedFileSize += file.size;

      // Ensure we show 100% completion for the last file
      if (fileIndex === selectedFiles.length - 1) {
        updateProgress(100);
      }

      results.push({
        name: newFileName,
        data: processedData,
        originalSize: data.length,
        processedSize: processedData.length,
      });

      totalOriginalSize += data.length;
      totalProcessedSize += processedData.length;
    }

    processedData = results;

    // Show results
    const ratio = totalOriginalSize > 0 ? (totalOriginalSize / totalProcessedSize).toFixed(2) : '0';
    const savings = totalOriginalSize > 0 ? ((1 - totalProcessedSize / totalOriginalSize) * 100).toFixed(1) : '0';

    const stats = [
      { label: 'Uncompressed Size', value: formatBytes(totalOriginalSize) },
      {
        label: 'Compressed Size',
        value: formatBytes(totalProcessedSize),
      },
      { label: 'Compression Ratio', value: `${ratio}:1` },
      { label: currentOperation === 'compress' ? 'Space Saved' : 'Size Change', value: `${savings}%` },
      { label: 'Time', value: `${totalProcessingTime.toFixed(2)} ms` },
    ];

    showResults('Complete', stats);
  } catch (error) {
    const errorStats = [
      { value: error.name || 'Error', label: 'Error Type' },
      { value: selectedFiles.length.toString(), label: 'Files Affected' },
    ];

    showResults('Error', errorStats, true);

    // Show error message
    const errorDiv = document.createElement('div');
    errorDiv.className = 'error-message';
    errorDiv.textContent = error.message || 'An unknown error occurred';
    results.appendChild(errorDiv);
  }

  hideLoading();
}

function downloadResult() {
  if (!processedData || processedData.length === 0) return;

  if (processedData.length === 1) {
    // Single file download
    const result = processedData[0];
    const blob = new Blob([result.data], { type: 'application/octet-stream' });
    downloadBlob(blob, result.name);
  } else {
    // Multiple files - create a zip-like structure or download individually
    processedData.forEach(result => {
      const blob = new Blob([result.data], { type: 'application/octet-stream' });
      downloadBlob(blob, result.name);
    });
  }
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

function handleFileSelection(files) {
  selectedFiles = files;
  updateFileInfo();
}

function showTextStats(title, stats) {
  textStatsTitle.textContent = title;
  textStatsContent.innerHTML = '';

  stats.forEach(stat => {
    const statElement = document.createElement('div');
    statElement.className = 'text-stat';
    statElement.innerHTML = `
            <span>${stat.label}</span>
            <span>${stat.value}</span>
        `;
    textStatsContent.appendChild(statElement);
  });

  textStats.classList.add('show');
}

function hideTextStats() {
  textStats.classList.remove('show');
}

function validateCustomDictionary(dictionaryText, windowBits) {
  if (!dictionaryText) {
    return { isValid: true, dictionaryBytes: null };
  }

  const windowSize = 1 << windowBits;
  const dictionaryBytes = new TextEncoder().encode(dictionaryText);

  if (dictionaryBytes.length > windowSize) {
    return {
      isValid: false,
      error: `Dictionary size (${dictionaryBytes.length} bytes) exceeds window size (${windowSize} bytes)`,
    };
  }

  return { isValid: true, dictionaryBytes: dictionaryBytes };
}

async function createProcessedDictionary(dictionaryBytes, windowBits) {
  if (!dictionaryBytes) {
    return null;
  }

  const windowSize = 1 << windowBits;
  // Initialize dictionary with proper defaults, then fill the last part with the data
  const paddedDictionary = await initializeDictionary(windowSize);
  paddedDictionary.set(dictionaryBytes, windowSize - dictionaryBytes.length);

  return paddedDictionary;
}

function showDictionaryError(message) {
  dictionaryError.textContent = message;
  dictionaryError.classList.remove('hidden');
}

function hideDictionaryError() {
  dictionaryError.classList.add('hidden');
}

function toggleCustomDictionary() {
  const isCurrentlyHidden = customDictionarySection.classList.contains('hidden');

  if (isCurrentlyHidden) {
    // Show the dictionary section
    customDictionarySection.classList.remove('hidden');
    toggleDictionaryBtn.classList.add('active');
    toggleDictionaryBtn.textContent = '− Hide Custom Dictionary';
  } else {
    // Hide the dictionary section
    customDictionarySection.classList.add('hidden');
    toggleDictionaryBtn.classList.remove('active');
    toggleDictionaryBtn.textContent = '+ Use Custom Dictionary';

    // Clear the dictionary input and hide any errors
    customDictionaryTextArea.value = '';
    hideDictionaryError();
  }
}

async function compressTextContent() {
  const plainText = plainTextArea.value;

  if (!plainText) {
    alert('Enter text to compress.');
    return;
  }

  // Ensure WASM is initialized before processing
  if (!wasmInitialized) {
    await wasmInitPromise;
  }

  const windowBits = parseInt(textWindowBitsSelect.value);
  const customDictionaryText = customDictionarySection.classList.contains('hidden')
    ? ''
    : customDictionaryTextArea.value;

  // Validate custom dictionary (only if section is visible)
  const dictionaryValidation = validateCustomDictionary(customDictionaryText, windowBits);
  if (!dictionaryValidation.isValid) {
    showDictionaryError(dictionaryValidation.error);
    return;
  }

  hideDictionaryError();

  // Show progress for text compression too
  showLoading();

  try {
    const options = {
      window: windowBits,
      // Add progress callback for text compression
      onPoll: async progressInfo => {
        const bytesProcessed = progressInfo.bytesProcessed || 0;
        const totalBytes = progressInfo.totalBytes || 0;

        const percentage = totalBytes > 0 ? (bytesProcessed / totalBytes) * 100 : 0;
        await updateProgress(percentage);
      },
    };

    if (textLazyMatchingCheckbox.checked) {
      options.lazy_matching = true;
    }

    // Add custom dictionary if provided
    const processedDictionary = await createProcessedDictionary(dictionaryValidation.dictionaryBytes, windowBits);
    if (processedDictionary) {
      options.dictionary = processedDictionary;
    }

    // Detect if input is pure ASCII (all characters < 128)
    const isPureAscii = plainText.split('').every(char => char.charCodeAt(0) < 128);
    if (isPureAscii) {
      options.literal = 7;
    }

    const data = new TextEncoder().encode(plainText);

    const startTime = performance.now();
    const compressed = await compress(data, options);
    const endTime = performance.now();
    const compressionTime = endTime - startTime;

    // Convert to base64 for display
    const base64 = btoa(String.fromCharCode(...compressed));
    compressedTextArea.value = base64;

    // Calculate and display compression stats
    const ratio = data.length > 0 ? (data.length / compressed.length).toFixed(2) : '0';
    const savings = data.length > 0 ? ((1 - compressed.length / data.length) * 100).toFixed(1) : '0';

    const configStr = `${windowBits}-bit window${isPureAscii ? ', 7-bit literals' : ''}${
      textLazyMatchingCheckbox.checked ? ', lazy matching' : ''
    }${dictionaryValidation.dictionaryBytes ? ', custom dictionary' : ''}`;

    const stats = [
      { label: 'Configuration', value: configStr },
      { label: 'Uncompressed Size', value: `${data.length.toLocaleString()} bytes` },
      { label: 'Compressed Size', value: `${compressed.length.toLocaleString()} bytes` },
      { label: 'Compression Ratio', value: `${ratio}:1` },
      { label: 'Space Saved', value: `${savings}%` },
      { label: 'Time', value: `${compressionTime.toFixed(2)} ms` },
      { label: 'ASCII Optimized', value: isPureAscii ? 'Yes' : 'No' },
    ];

    showTextStats('Compression Results', stats);
  } catch (error) {
    alert('Compression error: ' + error.message);
    hideTextStats();
  } finally {
    hideLoading();
  }
}

async function decompressTextContent() {
  const compressedBase64 = compressedTextArea.value;

  if (!compressedBase64.trim()) {
    alert('Enter base64-encoded tamp compressed data to decompress.');
    return;
  }

  // Ensure WASM is initialized before processing
  if (!wasmInitialized) {
    await wasmInitPromise;
  }

  const windowBits = parseInt(textWindowBitsSelect.value);
  const customDictionaryText = customDictionarySection.classList.contains('hidden')
    ? ''
    : customDictionaryTextArea.value;

  // Validate custom dictionary (only if section is visible)
  const dictionaryValidation = validateCustomDictionary(customDictionaryText, windowBits);
  if (!dictionaryValidation.isValid) {
    showDictionaryError(dictionaryValidation.error);
    return;
  }

  hideDictionaryError();

  try {
    // Convert from base64
    const binaryString = atob(compressedBase64);
    const compressed = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
      compressed[i] = binaryString.charCodeAt(i);
    }

    const options = {};

    // Add custom dictionary if provided
    const processedDictionary = await createProcessedDictionary(dictionaryValidation.dictionaryBytes, windowBits);
    if (processedDictionary) {
      options.dictionary = processedDictionary;
    }

    const startTime = performance.now();
    const decompressed = await decompress(compressed, options);
    const endTime = performance.now();
    const decompressionTime = endTime - startTime;

    const text = new TextDecoder().decode(decompressed);

    plainTextArea.value = text;

    // Calculate and display decompression stats (same format as compression)
    const ratio = compressed.length > 0 ? (decompressed.length / compressed.length).toFixed(2) : '0';
    const sizeChange = compressed.length > 0 ? ((decompressed.length / compressed.length - 1) * 100).toFixed(1) : '0';

    // Check if decompressed text is ASCII
    const isAscii = text.split('').every(char => char.charCodeAt(0) < 128);

    const stats = [
      {
        label: 'Configuration',
        value: `Decompression (auto-detected)${dictionaryValidation.dictionaryBytes ? ', custom dictionary' : ''}`,
      },
      { label: 'Uncompressed Size', value: `${decompressed.length.toLocaleString()} bytes` },
      { label: 'Compressed Size', value: `${compressed.length.toLocaleString()} bytes` },
      { label: 'Compression Ratio', value: `${ratio}:1` },
      { label: 'Size Increase', value: `${sizeChange}%` },
      { label: 'Time', value: `${decompressionTime.toFixed(2)} ms` },
      { label: 'ASCII Optimized', value: isAscii ? 'Yes' : 'No' },
    ];

    showTextStats('Decompression Results', stats);
  } catch (error) {
    alert('Decompression error: ' + error.message);
    hideTextStats();
  }
}
