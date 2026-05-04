import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Group, Image as KonvaImage, Layer, Line, Rect, Stage, Text, Transformer } from 'react-konva';
import { SCENE_PRESETS, cloneDocument, getPresetById } from './presets';
import { useLoadedImage } from './useLoadedImage';
import type { BackgroundFit, EditorDocument, EditorLayer, ImageLayer, ScenePreset, TextLayer } from './types';

const HISTORY_LIMIT = 60;
const FONT_OPTIONS = ['Sora', 'IBM Plex Sans', 'Georgia', 'Courier New'];

type InspectorMenu = 'scene' | 'copy' | 'media' | 'layers' | 'export';

type OpenSectionState = Record<InspectorMenu, string>;

const DEFAULT_OPEN_SECTIONS: OpenSectionState = {
  scene: 'scene-presets',
  copy: 'copy-insert',
  media: 'media-library',
  layers: 'layers-selection',
  export: 'export-output',
};

const MENU_ITEMS: Array<{
  id: InspectorMenu;
  label: string;
  note: string;
}> = [
  { id: 'scene', label: '场景', note: '预设与背景' },
  { id: 'copy', label: '文案', note: '文字与排版' },
  { id: 'media', label: '素材', note: '图片与视觉' },
  { id: 'layers', label: '图层', note: '层级与命名' },
  { id: 'export', label: '导出', note: '成品与快照' },
];

function uid(prefix: string) {
  return `${prefix}-${crypto.randomUUID()}`;
}

function downloadFile(filename: string, blob: Blob) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

function parseHexColor(color: string) {
  if (/^#[0-9a-fA-F]{6}$/.test(color)) {
    return `${color}ff`;
  }
  return color;
}

function getCoverCrop(imageWidth: number, imageHeight: number, frameWidth: number, frameHeight: number) {
  const imageRatio = imageWidth / imageHeight;
  const frameRatio = frameWidth / frameHeight;

  if (imageRatio > frameRatio) {
    const cropWidth = imageHeight * frameRatio;
    return {
      x: (imageWidth - cropWidth) / 2,
      y: 0,
      width: cropWidth,
      height: imageHeight,
    };
  }

  const cropHeight = imageWidth / frameRatio;
  return {
    x: 0,
    y: (imageHeight - cropHeight) / 2,
    width: imageWidth,
    height: cropHeight,
  };
}

function getContainFrame(imageWidth: number, imageHeight: number, frameWidth: number, frameHeight: number) {
  const scale = Math.min(frameWidth / imageWidth, frameHeight / imageHeight);
  const width = imageWidth * scale;
  const height = imageHeight * scale;

  return {
    x: (frameWidth - width) / 2,
    y: (frameHeight - height) / 2,
    width,
    height,
  };
}

function getTextEditorMinHeight(layer: TextLayer) {
  const lineCount = Math.max(1, layer.text.split('\n').length);
  return Math.max(56, Math.ceil(lineCount * layer.fontSize * layer.lineHeight + 12));
}

function isPointInsideTextLayer(layer: TextLayer, x: number, y: number) {
  const height = getTextEditorMinHeight(layer);
  return x >= layer.x && x <= layer.x + layer.width && y >= layer.y && y <= layer.y + height;
}

function createTextLayer(kind: 'headline' | 'body' | 'caption', document: EditorDocument): TextLayer {
  const baseOffset = document.layers.length * 18;

  if (kind === 'headline') {
    return {
      id: uid('text'),
      type: 'text',
      name: 'Headline',
      text: 'New headline',
      x: 96 + baseOffset,
      y: 126 + baseOffset,
      width: 640,
      fontSize: 68,
      fontFamily: 'Sora',
      fontWeight: 700,
      fill: '#f7f3ed',
      align: 'left',
      lineHeight: 1.05,
      letterSpacing: -0.8,
      rotation: 0,
      opacity: 1,
      visible: true,
      locked: false,
    };
  }

  if (kind === 'body') {
    return {
      id: uid('text'),
      type: 'text',
      name: 'Body Copy',
      text: 'Add supporting copy here.',
      x: 96 + baseOffset,
      y: 522 + baseOffset,
      width: 440,
      fontSize: 24,
      fontFamily: 'IBM Plex Sans',
      fontWeight: 500,
      fill: '#d9e7ed',
      align: 'left',
      lineHeight: 1.28,
      letterSpacing: 0,
      rotation: 0,
      opacity: 1,
      visible: true,
      locked: false,
    };
  }

  return {
    id: uid('text'),
    type: 'text',
    name: 'Caption',
    text: 'CTA or note',
    x: 96 + baseOffset,
    y: 720 + baseOffset,
    width: 360,
    fontSize: 18,
    fontFamily: 'IBM Plex Sans',
    fontWeight: 600,
    fill: '#f2c66d',
    align: 'left',
    lineHeight: 1.1,
    letterSpacing: 0,
    rotation: 0,
    opacity: 1,
    visible: true,
    locked: false,
  };
}

function createImageLayer(src: string, name: string): ImageLayer {
  return {
    id: uid('image'),
    type: 'image',
    name,
    src,
    x: 744,
    y: 144,
    width: 320,
    height: 460,
    stroke: '#ffffff',
    strokeWidth: 1.5,
    shadowColor: '#081018',
    shadowBlur: 22,
    shadowOpacity: 0.24,
    rotation: 0,
    opacity: 1,
    visible: true,
    locked: false,
  };
}

function createHistoryDocument(presetId: ScenePreset['id']) {
  return cloneDocument(getPresetById(presetId).document);
}

function MenuSection({
  title,
  summary,
  badge,
  open,
  onToggle,
  children,
}: {
  title: string;
  summary: string;
  badge?: string;
  open: boolean;
  onToggle: () => void;
  children: ReactNode;
}) {
  return (
    <section className={`menu-section ${open ? 'is-open' : ''}`}>
      <button type="button" className="menu-section__trigger" onClick={onToggle}>
        <span className="menu-section__title">
          <strong>{title}</strong>
          <small>{summary}</small>
        </span>
        <span className="menu-section__side">
          {badge ? <span className="menu-section__badge">{badge}</span> : null}
          <span className="menu-section__chevron">{open ? '收起' : '展开'}</span>
        </span>
      </button>
      {open ? <div className="menu-section__body">{children}</div> : null}
    </section>
  );
}

function BackgroundArtwork({
  document,
}: {
  document: EditorDocument;
}) {
  const backgroundImage = useLoadedImage(document.background.imageSrc);
  const crop =
    backgroundImage && document.background.fit === 'cover'
      ? getCoverCrop(backgroundImage.width, backgroundImage.height, document.canvas.width, document.canvas.height)
      : null;
  const containFrame =
    backgroundImage && document.background.fit === 'contain'
      ? getContainFrame(backgroundImage.width, backgroundImage.height, document.canvas.width, document.canvas.height)
      : null;

  return (
    <>
      <Rect x={0} y={0} width={document.canvas.width} height={document.canvas.height} fill={document.background.color} listening={false} />

      {backgroundImage && crop ? (
        <KonvaImage
          image={backgroundImage}
          width={document.canvas.width}
          height={document.canvas.height}
          crop={crop}
          opacity={document.background.imageOpacity}
          listening={false}
        />
      ) : null}

      {backgroundImage && containFrame ? (
        <KonvaImage
          image={backgroundImage}
          x={containFrame.x}
          y={containFrame.y}
          width={containFrame.width}
          height={containFrame.height}
          opacity={document.background.imageOpacity}
          listening={false}
        />
      ) : null}

      <Rect
        x={0}
        y={0}
        width={document.canvas.width}
        height={document.canvas.height}
        fill={document.background.overlayColor}
        opacity={document.background.overlayOpacity}
        listening={false}
      />

      <Rect
        x={34}
        y={34}
        width={document.canvas.width - 68}
        height={document.canvas.height - 68}
        stroke="rgba(255,255,255,0.12)"
        dash={[10, 12]}
        listening={false}
      />
      <Line
        points={[document.canvas.width / 2, 34, document.canvas.width / 2, document.canvas.height - 34]}
        stroke="rgba(255,255,255,0.08)"
        dash={[8, 12]}
        listening={false}
      />
      <Line
        points={[34, document.canvas.height / 2, document.canvas.width - 34, document.canvas.height / 2]}
        stroke="rgba(255,255,255,0.06)"
        dash={[8, 12]}
        listening={false}
      />
    </>
  );
}

function CanvasTextNode({
  layer,
  selected,
  editing,
  onSelect,
  onStartEditing,
  onChange,
}: {
  layer: TextLayer;
  selected: boolean;
  editing: boolean;
  onSelect: () => void;
  onStartEditing: () => void;
  onChange: (updates: Partial<TextLayer>) => void;
}) {
  const shapeRef = useRef<any>(null);
  const transformerRef = useRef<any>(null);

  useEffect(() => {
    if (!selected || !transformerRef.current || !shapeRef.current) {
      return;
    }

    transformerRef.current.nodes([shapeRef.current]);
    transformerRef.current.getLayer()?.batchDraw();
  }, [selected]);

  return (
    <>
      <Text
        ref={shapeRef}
        x={layer.x}
        y={layer.y}
        width={layer.width}
        text={layer.text}
        fontSize={layer.fontSize}
        fontFamily={layer.fontFamily}
        fontStyle={layer.fontWeight >= 600 ? 'bold' : 'normal'}
        fill={layer.fill}
        align={layer.align}
        lineHeight={layer.lineHeight}
        letterSpacing={layer.letterSpacing}
        rotation={layer.rotation}
        opacity={layer.opacity}
        visible={layer.visible && !editing}
        draggable={!layer.locked && !editing}
        perfectDrawEnabled={false}
        onClick={(event) => {
          if (selected && !editing) {
            onStartEditing();
            return;
          }
          onSelect();
          if (event.evt.detail >= 2) {
            onStartEditing();
          }
        }}
        onDblClick={() => {
          onStartEditing();
        }}
        onDragEnd={(event) => {
          onChange({
            x: event.target.x(),
            y: event.target.y(),
          });
        }}
        onTransformEnd={() => {
          const node = shapeRef.current;
          const scaleX = node.scaleX();
          const scaleY = node.scaleY();
          node.scaleX(1);
          node.scaleY(1);
          onChange({
            x: node.x(),
            y: node.y(),
            rotation: node.rotation(),
            width: Math.max(120, node.width() * scaleX),
            fontSize: Math.max(16, layer.fontSize * scaleY),
          });
        }}
      />
      {selected && !editing ? (
        <Transformer
          ref={transformerRef}
          rotateEnabled={!layer.locked}
          enabledAnchors={layer.locked ? [] : ['top-left', 'top-right', 'middle-left', 'middle-right', 'bottom-left', 'bottom-right']}
          boundBoxFunc={(_, nextBox) => ({
            ...nextBox,
            width: Math.max(120, nextBox.width),
            height: Math.max(42, nextBox.height),
          })}
          borderStroke="#8dd9ff"
          anchorFill="#fff"
          anchorStroke="#09131c"
          anchorCornerRadius={12}
          anchorSize={12}
        />
      ) : null}
    </>
  );
}

function CanvasImageNode({
  layer,
  selected,
  onSelect,
  onChange,
}: {
  layer: ImageLayer;
  selected: boolean;
  onSelect: () => void;
  onChange: (updates: Partial<ImageLayer>) => void;
}) {
  const image = useLoadedImage(layer.src);
  const groupRef = useRef<any>(null);
  const transformerRef = useRef<any>(null);

  useEffect(() => {
    if (!selected || !transformerRef.current || !groupRef.current) {
      return;
    }

    transformerRef.current.nodes([groupRef.current]);
    transformerRef.current.getLayer()?.batchDraw();
  }, [selected]);

  return (
    <>
      <Group
        ref={groupRef}
        x={layer.x}
        y={layer.y}
        rotation={layer.rotation}
        opacity={layer.opacity}
        visible={layer.visible}
        draggable={!layer.locked}
        onClick={onSelect}
        onDragEnd={(event) => {
          onChange({
            x: event.target.x(),
            y: event.target.y(),
          });
        }}
        onTransformEnd={() => {
          const node = groupRef.current;
          const scaleX = node.scaleX();
          const scaleY = node.scaleY();
          node.scaleX(1);
          node.scaleY(1);
          onChange({
            x: node.x(),
            y: node.y(),
            rotation: node.rotation(),
            width: Math.max(120, layer.width * scaleX),
            height: Math.max(120, layer.height * scaleY),
          });
        }}
      >
        <Rect
          width={layer.width}
          height={layer.height}
          fill="rgba(255,255,255,0.06)"
          stroke={layer.stroke}
          strokeWidth={layer.strokeWidth}
          shadowColor={layer.shadowColor}
          shadowBlur={layer.shadowBlur}
          shadowOpacity={layer.shadowOpacity}
          listening={false}
        />
        {image ? (
          <KonvaImage
            image={image}
            width={layer.width}
            height={layer.height}
            stroke={layer.stroke}
            strokeWidth={layer.strokeWidth}
            shadowColor={layer.shadowColor}
            shadowBlur={layer.shadowBlur}
            shadowOpacity={layer.shadowOpacity}
          />
        ) : (
          <Rect width={layer.width} height={layer.height} fill="rgba(255,255,255,0.08)" stroke={layer.stroke} strokeWidth={layer.strokeWidth} />
        )}
      </Group>
      {selected ? (
        <Transformer
          ref={transformerRef}
          rotateEnabled={!layer.locked}
          enabledAnchors={layer.locked ? [] : undefined}
          boundBoxFunc={(_, nextBox) => ({
            ...nextBox,
            width: Math.max(120, nextBox.width),
            height: Math.max(120, nextBox.height),
          })}
          borderStroke="#ff845f"
          anchorFill="#fff"
          anchorStroke="#1b2431"
          anchorCornerRadius={12}
          anchorSize={12}
        />
      ) : null}
    </>
  );
}

export default function App() {
  const [selectedPresetId, setSelectedPresetId] = useState<ScenePreset['id']>('atelier');
  const [history, setHistory] = useState(() => ({
    past: [] as EditorDocument[],
    present: createHistoryDocument('atelier'),
    future: [] as EditorDocument[],
  }));
  const [selectedLayerId, setSelectedLayerId] = useState<string | null>(null);
  const [editingTextLayerId, setEditingTextLayerId] = useState<string | null>(null);
  const [uploadIntent, setUploadIntent] = useState<{ mode: 'background' | 'new-image' | 'replace-image'; layerId?: string } | null>(null);
  const [activeMenu, setActiveMenu] = useState<InspectorMenu>('scene');
  const [openSections, setOpenSections] = useState<OpenSectionState>(DEFAULT_OPEN_SECTIONS);

  const stageRef = useRef<any>(null);
  const uploadInputRef = useRef<HTMLInputElement | null>(null);
  const inlineEditorRef = useRef<HTMLTextAreaElement | null>(null);
  const stageViewportRef = useRef<HTMLDivElement | null>(null);
  const objectUrlsRef = useRef<string[]>([]);

  const currentPreset = getPresetById(selectedPresetId);
  const selectedLayer = history.present.layers.find((layer) => layer.id === selectedLayerId) ?? null;
  const editingTextLayer =
    editingTextLayerId == null
      ? null
      : history.present.layers.find((layer): layer is TextLayer => layer.type === 'text' && layer.id === editingTextLayerId) ?? null;

  const textLayers = history.present.layers.filter((layer): layer is TextLayer => layer.type === 'text');
  const imageLayers = history.present.layers.filter((layer): layer is ImageLayer => layer.type === 'image');
  const visibleLayerCount = history.present.layers.filter((layer) => layer.visible).length;
  const reversedLayers = [...history.present.layers].reverse();

  const canUndo = history.past.length > 0;
  const canRedo = history.future.length > 0;

  const inspectorTitle = !selectedLayer ? 'Scene background' : selectedLayer.name;
  const inspectorDescription = !selectedLayer
    ? '先在右侧选择场景菜单，再一步步处理背景、文案和素材。画布空白处会回到场景级编辑。'
    : selectedLayer.type === 'text'
      ? '当前选中了文字层。右侧会优先展示内容、排版和布局选择，而不是把所有控件一次性堆出来。'
      : '当前选中了图片层。右侧会优先展示替换、尺寸和视觉样式，方便更快完成单次任务。';
  const workspaceTitle = !selectedLayer ? '海报编辑器' : `编辑 ${selectedLayer.name}`;
  const workspaceNote = !selectedLayer
    ? '统一处理背景、文案和素材，实时查看当前画面的结果。'
    : selectedLayer.type === 'text'
      ? '当前是文字对象，可直接修改文案、排版和样式。'
      : '当前是图片对象，可直接替换素材并调整视觉样式。';

  const activeMenuMeta = MENU_ITEMS.find((item) => item.id === activeMenu) ?? MENU_ITEMS[0];

  useEffect(() => {
    return () => {
      objectUrlsRef.current.forEach((url) => URL.revokeObjectURL(url));
    };
  }, []);

  useEffect(() => {
    if (selectedLayerId && !history.present.layers.some((layer) => layer.id === selectedLayerId)) {
      setSelectedLayerId(null);
    }
  }, [history.present.layers, selectedLayerId]);

  useEffect(() => {
    if (editingTextLayerId && !history.present.layers.some((layer) => layer.type === 'text' && layer.id === editingTextLayerId)) {
      setEditingTextLayerId(null);
    }
  }, [editingTextLayerId, history.present.layers]);

  useEffect(() => {
    if (!selectedLayerId) {
      setEditingTextLayerId(null);
      setActiveMenu('scene');
      setOpenSections((current) => ({ ...current, scene: current.scene || 'scene-presets' }));
      return;
    }

    if (selectedLayer?.type === 'text') {
      setActiveMenu('copy');
      setOpenSections((current) => ({ ...current, copy: current.copy || 'copy-layout' }));
      return;
    }

    if (selectedLayer?.type === 'image') {
      setEditingTextLayerId(null);
      setActiveMenu('media');
      setOpenSections((current) => ({ ...current, media: 'media-adjust' }));
    }
  }, [selectedLayer?.type, selectedLayerId]);

  useEffect(() => {
    if (!editingTextLayer) {
      return;
    }

    const editor = inlineEditorRef.current;
    if (!editor) {
      return;
    }

    editor.focus();
    editor.setSelectionRange(editor.value.length, editor.value.length);
  }, [editingTextLayer?.id]);

  useEffect(() => {
    if (!editingTextLayer) {
      return;
    }

    const editor = inlineEditorRef.current;
    if (!editor) {
      return;
    }

    editor.style.height = '0px';
    editor.style.height = `${editor.scrollHeight}px`;
  }, [
    editingTextLayer?.align,
    editingTextLayer?.fontFamily,
    editingTextLayer?.fontSize,
    editingTextLayer?.fontWeight,
    editingTextLayer?.letterSpacing,
    editingTextLayer?.lineHeight,
    editingTextLayer?.text,
    editingTextLayer?.width,
  ]);

  useEffect(() => {
    function handleKeyboard(event: KeyboardEvent) {
      const target = event.target as HTMLElement | null;
      const isTypingTarget =
        target instanceof HTMLInputElement ||
        target instanceof HTMLTextAreaElement ||
        target instanceof HTMLSelectElement ||
        target?.isContentEditable;

      if (isTypingTarget) {
        return;
      }

      const modifier = event.metaKey || event.ctrlKey;

      if (modifier && event.key.toLowerCase() === 'z') {
        event.preventDefault();
        if (event.shiftKey) {
          redo();
          return;
        }
        undo();
        return;
      }

      if ((modifier && event.key.toLowerCase() === 'y') || (modifier && event.shiftKey && event.key.toLowerCase() === 'z')) {
        event.preventDefault();
        redo();
        return;
      }

      if (modifier && event.key.toLowerCase() === 'd' && selectedLayerId) {
        event.preventDefault();
        duplicateSelectedLayer();
        return;
      }

      if ((event.key === 'Backspace' || event.key === 'Delete') && selectedLayerId) {
        event.preventDefault();
        removeSelectedLayer();
      }
    }

    window.addEventListener('keydown', handleKeyboard);
    return () => window.removeEventListener('keydown', handleKeyboard);
  }, [selectedLayerId, history.present]);

  function setMenu(menu: InspectorMenu, sectionId?: string) {
    setActiveMenu(menu);
    if (sectionId) {
      setOpenSections((current) => ({ ...current, [menu]: sectionId }));
    }
  }

  function toggleSection(menu: InspectorMenu, sectionId: string) {
    setOpenSections((current) => ({
      ...current,
      [menu]: current[menu] === sectionId ? '' : sectionId,
    }));
  }

  function activateLayer(layerId: string) {
    setEditingTextLayerId((current) => (current === layerId ? current : null));
    setSelectedLayerId(layerId);
  }

  function startTextEditing(layerId: string) {
    setSelectedLayerId(layerId);
    setEditingTextLayerId(layerId);
    setMenu('copy', 'copy-layout');
  }

  function finishTextEditing() {
    setEditingTextLayerId(null);
  }

  function findTextLayerAtPoint(x: number, y: number) {
    return [...history.present.layers]
      .reverse()
      .find((layer): layer is TextLayer => layer.type === 'text' && layer.visible && isPointInsideTextLayer(layer, x, y));
  }

  function focusLayerOnCanvas(layerId: string) {
    const targetLayer = history.present.layers.find((layer) => layer.id === layerId);
    const viewport = stageViewportRef.current;

    if (!targetLayer || !viewport) {
      activateLayer(layerId);
      return;
    }

    activateLayer(layerId);
    viewport.scrollTo({
      left: Math.max(0, targetLayer.x - viewport.clientWidth / 2 + 220),
      top: Math.max(0, targetLayer.y - viewport.clientHeight / 2 + 140),
      behavior: 'smooth',
    });
  }

  function commitDocument(recipe: (draft: EditorDocument) => void) {
    setHistory((currentHistory) => {
      const nextDocument = cloneDocument(currentHistory.present);
      recipe(nextDocument);

      if (JSON.stringify(nextDocument) === JSON.stringify(currentHistory.present)) {
        return currentHistory;
      }

      return {
        past: [...currentHistory.past, currentHistory.present].slice(-HISTORY_LIMIT),
        present: nextDocument,
        future: [],
      };
    });
  }

  function replaceDocument(document: EditorDocument) {
    setHistory((currentHistory) => ({
      past: [...currentHistory.past, currentHistory.present].slice(-HISTORY_LIMIT),
      present: cloneDocument(document),
      future: [],
    }));
  }

  function undo() {
    setHistory((currentHistory) => {
      if (!currentHistory.past.length) {
        return currentHistory;
      }

      const previous = currentHistory.past[currentHistory.past.length - 1];
      return {
        past: currentHistory.past.slice(0, -1),
        present: previous,
        future: [currentHistory.present, ...currentHistory.future].slice(0, HISTORY_LIMIT),
      };
    });
  }

  function redo() {
    setHistory((currentHistory) => {
      if (!currentHistory.future.length) {
        return currentHistory;
      }

      const [next, ...rest] = currentHistory.future;
      return {
        past: [...currentHistory.past, currentHistory.present].slice(-HISTORY_LIMIT),
        present: next,
        future: rest,
      };
    });
  }

  function applyPreset(presetId: ScenePreset['id']) {
    const preset = getPresetById(presetId);
    setSelectedPresetId(presetId);
    setSelectedLayerId(null);
    setMenu('scene', 'scene-presets');
    replaceDocument(preset.document);
  }

  function addTextBlock(kind: 'headline' | 'body' | 'caption') {
    const nextLayer = createTextLayer(kind, history.present);
    commitDocument((draft) => {
      draft.layers.push(nextLayer);
    });
    setSelectedLayerId(nextLayer.id);
    setMenu('copy', kind === 'caption' ? 'copy-insert' : 'copy-layout');
  }

  function addImageLayer(src: string, name: string) {
    const nextLayer = createImageLayer(src, name);
    commitDocument((draft) => {
      draft.layers.push(nextLayer);
    });
    setSelectedLayerId(nextLayer.id);
    setMenu('media', 'media-adjust');
  }

  function updateLayerById(layerId: string, recipe: (layer: EditorLayer) => void) {
    commitDocument((draft) => {
      const targetLayer = draft.layers.find((layer) => layer.id === layerId);
      if (!targetLayer) {
        return;
      }
      recipe(targetLayer);
    });
  }

  function updateSelectedLayer(recipe: (layer: EditorLayer) => void) {
    if (!selectedLayerId) {
      return;
    }

    updateLayerById(selectedLayerId, recipe);
  }

  function duplicateSelectedLayer() {
    if (!selectedLayer) {
      return;
    }

    const duplicate = {
      ...cloneDocument({ canvas: history.present.canvas, background: history.present.background, layers: [selectedLayer] }).layers[0],
      id: uid(selectedLayer.type),
      name: `${selectedLayer.name} Copy`,
      x: selectedLayer.x + 28,
      y: selectedLayer.y + 28,
    } as EditorLayer;

    commitDocument((draft) => {
      draft.layers.push(duplicate);
    });
    setSelectedLayerId(duplicate.id);
    setMenu(selectedLayer.type === 'text' ? 'copy' : 'media', selectedLayer.type === 'text' ? 'copy-layout' : 'media-adjust');
  }

  function removeSelectedLayer() {
    if (!selectedLayerId) {
      return;
    }

    const removedId = selectedLayerId;
    commitDocument((draft) => {
      draft.layers = draft.layers.filter((layer) => layer.id !== removedId);
    });
    setSelectedLayerId(null);
    setMenu('scene', 'scene-presets');
  }

  function moveLayer(layerId: string, direction: 'up' | 'down') {
    commitDocument((draft) => {
      const currentIndex = draft.layers.findIndex((layer) => layer.id === layerId);
      if (currentIndex === -1) {
        return;
      }

      const nextIndex = direction === 'up' ? currentIndex + 1 : currentIndex - 1;
      if (nextIndex < 0 || nextIndex >= draft.layers.length) {
        return;
      }

      const [layer] = draft.layers.splice(currentIndex, 1);
      draft.layers.splice(nextIndex, 0, layer);
    });
  }

  function exportPng() {
    const dataUrl = stageRef.current?.toDataURL({
      pixelRatio: 2,
      mimeType: 'image/png',
    });
    if (!dataUrl) {
      return;
    }

    const anchor = document.createElement('a');
    anchor.href = dataUrl;
    anchor.download = `editor-scene-${selectedPresetId}.png`;
    anchor.click();
  }

  function exportJson() {
    downloadFile(
      `editor-scene-${selectedPresetId}.json`,
      new Blob([JSON.stringify(history.present, null, 2)], { type: 'application/json' }),
    );
  }

  function openUploadDialog(mode: 'background' | 'new-image' | 'replace-image', layerId?: string) {
    setUploadIntent({ mode, layerId });

    if (mode === 'background') {
      setMenu('scene', 'scene-background');
    }

    if (mode === 'new-image' || mode === 'replace-image') {
      setMenu('media', 'media-library');
    }

    uploadInputRef.current?.click();
  }

  function handleUploadChange(event: React.ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    const activeIntent = uploadIntent;
    event.target.value = '';

    if (!file || !activeIntent) {
      setUploadIntent(null);
      return;
    }

    const objectUrl = URL.createObjectURL(file);
    objectUrlsRef.current.push(objectUrl);

    if (activeIntent.mode === 'background') {
      commitDocument((draft) => {
        draft.background.imageSrc = objectUrl;
        draft.background.fit = 'cover';
        draft.background.imageOpacity = 1;
      });
      setSelectedLayerId(null);
      setMenu('scene', 'scene-background');
    }

    if (activeIntent.mode === 'new-image') {
      addImageLayer(objectUrl, file.name.replace(/\.[^.]+$/, '') || 'Uploaded Image');
    }

    if (activeIntent.mode === 'replace-image' && activeIntent.layerId) {
      const layerId = activeIntent.layerId;
      commitDocument((draft) => {
        const targetLayer = draft.layers.find((layer): layer is ImageLayer => layer.type === 'image' && layer.id === layerId);
        if (!targetLayer) {
          return;
        }
        targetLayer.src = objectUrl;
        targetLayer.name = file.name.replace(/\.[^.]+$/, '') || targetLayer.name;
      });
      setSelectedLayerId(layerId);
      setMenu('media', 'media-adjust');
    }

    setUploadIntent(null);
  }

  function renderSceneMenu() {
    return (
      <>
        <MenuSection
          title="先选一个场景方向"
          summary="先决定整体气质，再去微调细节。"
          badge={`${SCENE_PRESETS.length} 套预设`}
          open={openSections.scene === 'scene-presets'}
          onToggle={() => toggleSection('scene', 'scene-presets')}
        >
          <div className="choice-list">
            {SCENE_PRESETS.map((preset) => (
              <button
                key={preset.id}
                type="button"
                className={`choice-card choice-card--row ${preset.id === selectedPresetId ? 'is-active' : ''}`}
                onClick={() => applyPreset(preset.id)}
              >
                <span className="choice-thumb" style={{ backgroundImage: `url(${preset.thumbnail})` }} />
                <span className="choice-copy">
                  <strong>{preset.name}</strong>
                  <small>{preset.hint}</small>
                </span>
              </button>
            ))}
          </div>
        </MenuSection>

        <MenuSection
          title="再调背景氛围"
          summary="把背景图、底色和覆盖层分开处理。"
          badge={history.present.background.imageSrc ? '已挂背景图' : '纯色背景'}
          open={openSections.scene === 'scene-background'}
          onToggle={() => toggleSection('scene', 'scene-background')}
        >
          <div className="field-grid field-grid--two">
            <label className="field">
              <span>底色</span>
              <input
                type="color"
                value={parseHexColor(history.present.background.color).slice(0, 7)}
                onChange={(event) =>
                  commitDocument((draft) => {
                    draft.background.color = event.target.value;
                  })
                }
              />
            </label>

            <label className="field">
              <span>覆盖色</span>
              <input
                type="color"
                value={parseHexColor(history.present.background.overlayColor).slice(0, 7)}
                onChange={(event) =>
                  commitDocument((draft) => {
                    draft.background.overlayColor = event.target.value;
                  })
                }
              />
            </label>
          </div>

          <label className="field">
            <span>背景图透明度</span>
            <input
              type="range"
              min="0"
              max="100"
              value={Math.round(history.present.background.imageOpacity * 100)}
              onChange={(event) =>
                commitDocument((draft) => {
                  draft.background.imageOpacity = Number(event.target.value) / 100;
                })
              }
            />
            <small>{Math.round(history.present.background.imageOpacity * 100)}%</small>
          </label>

          <label className="field">
            <span>覆盖色强度</span>
            <input
              type="range"
              min="0"
              max="100"
              value={Math.round(history.present.background.overlayOpacity * 100)}
              onChange={(event) =>
                commitDocument((draft) => {
                  draft.background.overlayOpacity = Number(event.target.value) / 100;
                })
              }
            />
            <small>{Math.round(history.present.background.overlayOpacity * 100)}%</small>
          </label>

          <div className="segmented">
            {(['cover', 'contain'] as BackgroundFit[]).map((mode) => (
              <button
                key={mode}
                type="button"
                className={`segmented-btn ${history.present.background.fit === mode ? 'is-active' : ''}`}
                disabled={!history.present.background.imageSrc && mode === 'contain'}
                onClick={() =>
                  commitDocument((draft) => {
                    draft.background.fit = mode;
                  })
                }
              >
                {mode === 'cover' ? '铺满' : '完整显示'}
              </button>
            ))}
          </div>

          <div className="stack-actions">
            <button type="button" className="mini-btn" onClick={() => openUploadDialog('background')}>
              上传背景图
            </button>
            <button
              type="button"
              className="mini-btn"
              disabled={!history.present.background.imageSrc}
              onClick={() =>
                commitDocument((draft) => {
                  draft.background.imageSrc = undefined;
                })
              }
            >
              清空背景图
            </button>
          </div>
        </MenuSection>

        <MenuSection
          title="画布状态"
          summary="只看当前场景的关键结果。"
          badge={`${history.present.canvas.width} × ${history.present.canvas.height}`}
          open={openSections.scene === 'scene-status'}
          onToggle={() => toggleSection('scene', 'scene-status')}
        >
          <div className="stat-grid">
            <div className="stat-card">
              <span>当前预设</span>
              <strong>{currentPreset.name}</strong>
            </div>
            <div className="stat-card">
              <span>可见图层</span>
              <strong>{visibleLayerCount}</strong>
            </div>
            <div className="stat-card">
              <span>文字层</span>
              <strong>{textLayers.length}</strong>
            </div>
            <div className="stat-card">
              <span>图片层</span>
              <strong>{imageLayers.length}</strong>
            </div>
          </div>
        </MenuSection>
      </>
    );
  }

  function renderCopyMenu() {
    return (
      <>
        {selectedLayer?.type === 'text' ? (
          <section className="editor-card">
            <div className="editor-card__head">
              <span className="panel-kicker">当前文字</span>
              <strong>{selectedLayer.name}</strong>
              <p className="summary summary--compact">点选后右侧立即可改，双击或再次点击画布里的文字都能直接原位编辑。</p>
            </div>

            <label className="field">
              <span>图层名称</span>
              <input
                type="text"
                value={selectedLayer.name}
                onChange={(event) =>
                  updateSelectedLayer((layer) => {
                    if (layer.type !== 'text') {
                      return;
                    }
                    layer.name = event.target.value;
                  })
                }
              />
            </label>

            <label className="field">
              <span>文案内容</span>
              <textarea
                rows={6}
                spellCheck={false}
                value={selectedLayer.text}
                onChange={(event) =>
                  updateSelectedLayer((layer) => {
                    if (layer.type !== 'text') {
                      return;
                    }
                    layer.text = event.target.value;
                  })
                }
              />
            </label>

            <div className="stack-actions">
              <button type="button" className="mini-btn" onClick={duplicateSelectedLayer}>
                复制文字层
              </button>
              <button type="button" className="mini-btn" onClick={() => setMenu('copy', 'copy-layout')}>
                去排版样式
              </button>
            </div>
          </section>
        ) : null}

        <MenuSection
          title="先决定要插入哪种文案块"
          summary="从标题、正文和注释开始，而不是先掉进属性海洋。"
          badge={`${textLayers.length} 个文字层`}
          open={openSections.copy === 'copy-insert'}
          onToggle={() => toggleSection('copy', 'copy-insert')}
        >
          <div className="choice-grid">
            <button type="button" className="choice-card" onClick={() => addTextBlock('headline')}>
              <strong>主标题</strong>
              <small>用于大字开场和核心表达</small>
            </button>
            <button type="button" className="choice-card" onClick={() => addTextBlock('body')}>
              <strong>正文块</strong>
              <small>承接卖点、说明和补充信息</small>
            </button>
            <button type="button" className="choice-card" onClick={() => addTextBlock('caption')}>
              <strong>注释 / CTA</strong>
              <small>用于页脚、按钮文案和短句</small>
            </button>
          </div>
          <div className="layer-choice-list">
            {textLayers.map((layer) => (
              <button
                key={layer.id}
                type="button"
                className={`layer-choice ${selectedLayerId === layer.id ? 'is-active' : ''}`}
                onClick={() => focusLayerOnCanvas(layer.id)}
              >
                <strong>{layer.name}</strong>
                <small>{layer.text.slice(0, 28) || '空文案层'}</small>
              </button>
            ))}
          </div>
        </MenuSection>

        <MenuSection
          title="排版与样式"
          summary="把字号、字重、宽度和字距拆开控制。"
          badge={selectedLayer?.type === 'text' ? `${selectedLayer.fontSize}px` : '等待选择'}
          open={openSections.copy === 'copy-layout'}
          onToggle={() => toggleSection('copy', 'copy-layout')}
        >
          {selectedLayer?.type === 'text' ? (
            <>
              <label className="field">
                <span>字体</span>
                <select
                  value={selectedLayer.fontFamily}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.fontFamily = event.target.value;
                    })
                  }
                >
                  {FONT_OPTIONS.map((font) => (
                    <option key={font} value={font}>
                      {font}
                    </option>
                  ))}
                </select>
              </label>

              <label className="field">
                <span>字号</span>
                <input
                  type="range"
                  min="16"
                  max="128"
                  value={selectedLayer.fontSize}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.fontSize = Number(event.target.value);
                    })
                  }
                />
                <small>{selectedLayer.fontSize}px</small>
              </label>

              <label className="field">
                <span>文本宽度</span>
                <input
                  type="range"
                  min="120"
                  max="760"
                  value={selectedLayer.width}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.width = Number(event.target.value);
                    })
                  }
                />
                <small>{selectedLayer.width}px</small>
              </label>

              <div className="field-grid field-grid--two">
                <label className="field">
                  <span>颜色</span>
                  <input
                    type="color"
                    value={parseHexColor(selectedLayer.fill).slice(0, 7)}
                    onChange={(event) =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'text') {
                          return;
                        }
                        layer.fill = event.target.value;
                      })
                    }
                  />
                </label>

                <label className="field">
                  <span>透明度</span>
                  <input
                    type="range"
                    min="0"
                    max="100"
                    value={Math.round(selectedLayer.opacity * 100)}
                    onChange={(event) =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'text') {
                          return;
                        }
                        layer.opacity = Number(event.target.value) / 100;
                      })
                    }
                  />
                  <small>{Math.round(selectedLayer.opacity * 100)}%</small>
                </label>
              </div>

              <div className="segmented">
                {[
                  { value: 400, label: 'Regular' },
                  { value: 600, label: 'Medium' },
                  { value: 700, label: 'Bold' },
                ].map((weight) => (
                  <button
                    key={weight.value}
                    type="button"
                    className={`segmented-btn ${selectedLayer.fontWeight === weight.value ? 'is-active' : ''}`}
                    onClick={() =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'text') {
                          return;
                        }
                        layer.fontWeight = weight.value;
                      })
                    }
                  >
                    {weight.label}
                  </button>
                ))}
              </div>

              <div className="segmented">
                {[
                  { value: 'left', label: '左对齐' },
                  { value: 'center', label: '居中' },
                  { value: 'right', label: '右对齐' },
                ].map((align) => (
                  <button
                    key={align.value}
                    type="button"
                    className={`segmented-btn ${selectedLayer.align === align.value ? 'is-active' : ''}`}
                    onClick={() =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'text') {
                          return;
                        }
                        layer.align = align.value as TextLayer['align'];
                      })
                    }
                  >
                    {align.label}
                  </button>
                ))}
              </div>

              <label className="field">
                <span>行高</span>
                <input
                  type="range"
                  min="0.9"
                  max="1.8"
                  step="0.02"
                  value={selectedLayer.lineHeight}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.lineHeight = Number(event.target.value);
                    })
                  }
                />
                <small>{selectedLayer.lineHeight.toFixed(2)}</small>
              </label>

              <label className="field">
                <span>字距</span>
                <input
                  type="range"
                  min="-2"
                  max="8"
                  step="0.1"
                  value={selectedLayer.letterSpacing}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.letterSpacing = Number(event.target.value);
                    })
                  }
                />
                <small>{selectedLayer.letterSpacing.toFixed(1)} px</small>
              </label>
            </>
          ) : (
            <div className="empty-state">
              <strong>这里会在你选中文字后展开</strong>
              <p>先从上面的“文字层列表”选一个对象，或者直接点画布中的标题、正文。</p>
            </div>
          )}
        </MenuSection>
      </>
    );
  }

  function renderMediaMenu() {
    return (
      <>
        {selectedLayer?.type === 'image' ? (
          <section className="editor-card">
            <div className="editor-card__head">
              <span className="panel-kicker">当前图片</span>
              <strong>{selectedLayer.name}</strong>
              <p className="summary summary--compact">替换、尺寸和透明度默认固定显示在这里。</p>
            </div>

            <label className="field">
              <span>图层名称</span>
              <input
                type="text"
                value={selectedLayer.name}
                onChange={(event) =>
                  updateSelectedLayer((layer) => {
                    if (layer.type !== 'image') {
                      return;
                    }
                    layer.name = event.target.value;
                  })
                }
              />
            </label>

            <div className="field-grid field-grid--two">
              <label className="field">
                <span>宽度</span>
                <input
                  type="range"
                  min="120"
                  max="960"
                  value={selectedLayer.width}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'image') {
                        return;
                      }
                      layer.width = Number(event.target.value);
                    })
                  }
                />
                <small>{Math.round(selectedLayer.width)} px</small>
              </label>

              <label className="field">
                <span>高度</span>
                <input
                  type="range"
                  min="120"
                  max="760"
                  value={selectedLayer.height}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'image') {
                        return;
                      }
                      layer.height = Number(event.target.value);
                    })
                  }
                />
                <small>{Math.round(selectedLayer.height)} px</small>
              </label>
            </div>

            <label className="field">
              <span>透明度</span>
              <input
                type="range"
                min="0"
                max="100"
                value={Math.round(selectedLayer.opacity * 100)}
                onChange={(event) =>
                  updateSelectedLayer((layer) => {
                    if (layer.type !== 'image') {
                      return;
                    }
                    layer.opacity = Number(event.target.value) / 100;
                  })
                }
              />
              <small>{Math.round(selectedLayer.opacity * 100)}%</small>
            </label>

            <div className="stack-actions">
              <button type="button" className="mini-btn" onClick={() => openUploadDialog('replace-image', selectedLayer.id)}>
                替换图片
              </button>
              <button type="button" className="mini-btn" onClick={duplicateSelectedLayer}>
                复制图片层
              </button>
            </div>
          </section>
        ) : null}

        <MenuSection
          title="先决定素材动作"
          summary="从新增图片、替换选中图片开始，不先暴露全部视觉参数。"
          badge={`${imageLayers.length} 个图片层`}
          open={openSections.media === 'media-library'}
          onToggle={() => toggleSection('media', 'media-library')}
        >
          <div className="choice-grid">
            <button type="button" className="choice-card" onClick={() => openUploadDialog('new-image')}>
              <strong>新增图片层</strong>
              <small>上传一张新素材，放进当前画布</small>
            </button>
            <button
              type="button"
              className="choice-card"
              disabled={selectedLayer?.type !== 'image'}
              onClick={() => {
                if (selectedLayer?.type === 'image') {
                  openUploadDialog('replace-image', selectedLayer.id);
                }
              }}
            >
              <strong>替换当前图片</strong>
              <small>保持位置不变，只替换画面内容</small>
            </button>
          </div>

          <div className="layer-choice-list">
            {imageLayers.map((layer) => (
              <button
                key={layer.id}
                type="button"
                className={`layer-choice ${selectedLayerId === layer.id ? 'is-active' : ''}`}
                onClick={() => focusLayerOnCanvas(layer.id)}
              >
                <strong>{layer.name}</strong>
                <small>{Math.round(layer.width)} × {Math.round(layer.height)}</small>
              </button>
            ))}
          </div>
        </MenuSection>

        <MenuSection
          title="调整当前图片"
          summary="尺寸、角度、边框和透明度都放在这一层。"
          badge={selectedLayer?.type === 'image' ? '已选中图片' : '未选中图片'}
          open={openSections.media === 'media-adjust'}
          onToggle={() => toggleSection('media', 'media-adjust')}
        >
          {selectedLayer?.type === 'image' ? (
            <>
              <label className="field">
                <span>旋转</span>
                <input
                  type="range"
                  min="-45"
                  max="45"
                  value={selectedLayer.rotation}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      if (layer.type !== 'image') {
                        return;
                      }
                      layer.rotation = Number(event.target.value);
                    })
                  }
                />
                <small>{selectedLayer.rotation}deg</small>
              </label>

              <div className="field-grid field-grid--two">
                <label className="field">
                  <span>描边颜色</span>
                  <input
                    type="color"
                    value={parseHexColor(selectedLayer.stroke).slice(0, 7)}
                    onChange={(event) =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'image') {
                          return;
                        }
                        layer.stroke = event.target.value;
                      })
                    }
                  />
                </label>

                <label className="field">
                  <span>描边粗细</span>
                  <input
                    type="range"
                    min="0"
                    max="8"
                    step="0.5"
                    value={selectedLayer.strokeWidth}
                    onChange={(event) =>
                      updateSelectedLayer((layer) => {
                        if (layer.type !== 'image') {
                          return;
                        }
                        layer.strokeWidth = Number(event.target.value);
                      })
                    }
                  />
                  <small>{selectedLayer.strokeWidth.toFixed(1)} px</small>
                </label>
              </div>

              <div className="stack-actions">
                <button type="button" className="mini-btn" onClick={() => setMenu('layers', 'layers-selection')}>
                  去图层管理
                </button>
              </div>
            </>
          ) : (
            <div className="empty-state">
              <strong>先选中图片层</strong>
              <p>点画布里的图片，或者从上面的图片层列表中选择一个对象，再继续调视觉样式。</p>
            </div>
          )}
        </MenuSection>
      </>
    );
  }

  function renderLayersMenu() {
    return (
      <>
        <MenuSection
          title="先确认当前选中"
          summary="命名、聚焦、显隐和锁定先收在一块。"
          badge={selectedLayer ? '已有选中对象' : '当前是场景'}
          open={openSections.layers === 'layers-selection'}
          onToggle={() => toggleSection('layers', 'layers-selection')}
        >
          {selectedLayer ? (
            <>
              <label className="field">
                <span>名称</span>
                <input
                  type="text"
                  value={selectedLayer.name}
                  onChange={(event) =>
                    updateSelectedLayer((layer) => {
                      layer.name = event.target.value;
                    })
                  }
                />
              </label>

              <div className="segmented">
                <button
                  type="button"
                  className={`segmented-btn ${selectedLayer.visible ? 'is-active' : ''}`}
                  onClick={() =>
                    updateSelectedLayer((layer) => {
                      layer.visible = !layer.visible;
                    })
                  }
                >
                  {selectedLayer.visible ? '当前可见' : '当前隐藏'}
                </button>
                <button
                  type="button"
                  className={`segmented-btn ${selectedLayer.locked ? 'is-active' : ''}`}
                  onClick={() =>
                    updateSelectedLayer((layer) => {
                      layer.locked = !layer.locked;
                    })
                  }
                >
                  {selectedLayer.locked ? '已锁定' : '可编辑'}
                </button>
              </div>

              <div className="stack-actions">
                <button type="button" className="mini-btn" onClick={() => focusLayerOnCanvas(selectedLayer.id)}>
                  画布聚焦
                </button>
                <button type="button" className="mini-btn" onClick={duplicateSelectedLayer}>
                  复制
                </button>
                <button type="button" className="mini-btn" onClick={removeSelectedLayer}>
                  删除
                </button>
              </div>
            </>
          ) : (
            <div className="empty-state">
              <strong>当前没有选中对象</strong>
              <p>点一下画布里的文字或图片，右侧就会把这个对象的图层动作收拢到这里。</p>
            </div>
          )}
        </MenuSection>

        <MenuSection
          title="图层顺序"
          summary="这里只做层级和导航，不在这里堆复杂属性。"
          badge={`${history.present.layers.length} 个图层`}
          open={openSections.layers === 'layers-stack'}
          onToggle={() => toggleSection('layers', 'layers-stack')}
        >
          <div className="stack-list">
            {reversedLayers.map((layer, index) => (
              <div key={layer.id} className={`stack-row ${selectedLayerId === layer.id ? 'is-active' : ''}`}>
                <button
                  type="button"
                  className="stack-main"
                  onClick={() => {
                    focusLayerOnCanvas(layer.id);
                    setMenu(layer.type === 'text' ? 'copy' : 'media', layer.type === 'text' ? 'copy-layout' : 'media-adjust');
                  }}
                >
                  <span className="layer-info">
                    <strong>{layer.name}</strong>
                    <small>
                      {layer.type === 'text' ? 'Text' : 'Image'} / {layer.visible ? 'Visible' : 'Hidden'}
                    </small>
                  </span>
                </button>
                <div className="stack-quick">
                  <button type="button" className="layer-btn" onClick={() => updateLayerById(layer.id, (target) => { target.visible = !target.visible; })}>
                    {layer.visible ? '显' : '隐'}
                  </button>
                  <button type="button" className="layer-btn" onClick={() => updateLayerById(layer.id, (target) => { target.locked = !target.locked; })}>
                    {layer.locked ? '锁' : '开'}
                  </button>
                  <button type="button" className="layer-btn" disabled={index === 0} onClick={() => moveLayer(layer.id, 'up')}>
                    上
                  </button>
                  <button
                    type="button"
                    className="layer-btn"
                    disabled={index === reversedLayers.length - 1}
                    onClick={() => moveLayer(layer.id, 'down')}
                  >
                    下
                  </button>
                </div>
              </div>
            ))}
          </div>
        </MenuSection>
      </>
    );
  }

  function renderExportMenu() {
    return (
      <>
        <MenuSection
          title="导出成品"
          summary="只保留最常用的导出动作。"
          badge="PNG / JSON"
          open={openSections.export === 'export-output'}
          onToggle={() => toggleSection('export', 'export-output')}
        >
          <div className="choice-grid">
            <button type="button" className="choice-card" onClick={exportPng}>
              <strong>导出 PNG</strong>
              <small>给评审、发群和快速预览</small>
            </button>
            <button type="button" className="choice-card" onClick={exportJson}>
              <strong>导出 JSON</strong>
              <small>保存当前场景结构和配置</small>
            </button>
          </div>
        </MenuSection>

        <MenuSection
          title="修改记录"
          summary="撤销和重做仍然保留，但不再占据主舞台。"
          badge={`${history.past.length} 步可撤销`}
          open={openSections.export === 'export-history'}
          onToggle={() => toggleSection('export', 'export-history')}
        >
          <div className="choice-grid">
            <button type="button" className="choice-card" disabled={!canUndo} onClick={undo}>
              <strong>撤销</strong>
              <small>回到上一步编辑状态</small>
            </button>
            <button type="button" className="choice-card" disabled={!canRedo} onClick={redo}>
              <strong>重做</strong>
              <small>恢复最近一次撤销</small>
            </button>
          </div>
        </MenuSection>

        <MenuSection
          title="当前交付概览"
          summary="导出前先快速确认你交出去的是什么。"
          badge={currentPreset.label}
          open={openSections.export === 'export-summary'}
          onToggle={() => toggleSection('export', 'export-summary')}
        >
          <div className="stat-grid">
            <div className="stat-card">
              <span>画布尺寸</span>
              <strong>
                {history.present.canvas.width} × {history.present.canvas.height}
              </strong>
            </div>
            <div className="stat-card">
              <span>文字层</span>
              <strong>{textLayers.length}</strong>
            </div>
            <div className="stat-card">
              <span>图片层</span>
              <strong>{imageLayers.length}</strong>
            </div>
            <div className="stat-card">
              <span>当前选中</span>
              <strong>{selectedLayer ? selectedLayer.name : 'Scene'}</strong>
            </div>
          </div>
        </MenuSection>
      </>
    );
  }

  function renderMenuContent() {
    if (activeMenu === 'scene') {
      return renderSceneMenu();
    }

    if (activeMenu === 'copy') {
      return renderCopyMenu();
    }

    if (activeMenu === 'media') {
      return renderMediaMenu();
    }

    if (activeMenu === 'layers') {
      return renderLayersMenu();
    }

    return renderExportMenu();
  }

  return (
    <div className="shell">
      <input ref={uploadInputRef} type="file" accept="image/*" hidden onChange={handleUploadChange} />

      <main className="workspace">
        <header className="workspace-head">
          <div className="workspace-title">
            <strong>{workspaceTitle}</strong>
            <p className="workspace-note">{workspaceNote}</p>
            <div className="workspace-meta">
              <span>场景 {currentPreset.name}</span>
              <span>对象 {selectedLayer ? selectedLayer.name : '整张画面'}</span>
              <span>可见图层 {visibleLayerCount}</span>
            </div>
          </div>
        </header>

        <section className="stage-wrap">
          <div className="stage-copy">
            <span>{currentPreset.kicker}</span>
            <strong>Canvas</strong>
          </div>
          <div ref={stageViewportRef} className="stage-scroll">
            <div
              className="stage-shell"
              style={{ width: history.present.canvas.width, height: history.present.canvas.height }}
              onDoubleClick={(event) => {
                const bounds = event.currentTarget.getBoundingClientRect();
                const hitLayer = findTextLayerAtPoint(event.clientX - bounds.left, event.clientY - bounds.top);
                if (!hitLayer) {
                  return;
                }
                startTextEditing(hitLayer.id);
              }}
            >
              <Stage
                ref={stageRef}
                width={history.present.canvas.width}
                height={history.present.canvas.height}
                onMouseDown={(event) => {
                  if (event.target === event.target.getStage()) {
                    finishTextEditing();
                    setSelectedLayerId(null);
                  }
                }}
              >
                <Layer>
                  <BackgroundArtwork document={history.present} />
                  {history.present.layers.map((layer) => {
                    if (layer.type === 'text') {
                      return (
                        <CanvasTextNode
                          key={layer.id}
                          layer={layer}
                          selected={selectedLayerId === layer.id}
                          editing={editingTextLayerId === layer.id}
                          onSelect={() => activateLayer(layer.id)}
                          onStartEditing={() => startTextEditing(layer.id)}
                          onChange={(updates) => {
                            commitDocument((draft) => {
                              const targetLayer = draft.layers.find(
                                (draftLayer): draftLayer is TextLayer => draftLayer.type === 'text' && draftLayer.id === layer.id,
                              );
                              if (!targetLayer) {
                                return;
                              }
                              Object.assign(targetLayer, updates);
                            });
                          }}
                        />
                      );
                    }

                    return (
                      <CanvasImageNode
                        key={layer.id}
                        layer={layer}
                        selected={selectedLayerId === layer.id}
                        onSelect={() => activateLayer(layer.id)}
                        onChange={(updates) => {
                          commitDocument((draft) => {
                            const targetLayer = draft.layers.find(
                              (draftLayer): draftLayer is ImageLayer => draftLayer.type === 'image' && draftLayer.id === layer.id,
                            );
                            if (!targetLayer) {
                              return;
                            }
                            Object.assign(targetLayer, updates);
                          });
                        }}
                      />
                    );
                  })}
                </Layer>
              </Stage>
              {editingTextLayer ? (
                <textarea
                  ref={inlineEditorRef}
                  className="stage-text-editor"
                  spellCheck={false}
                  value={editingTextLayer.text}
                  style={{
                    top: editingTextLayer.y - 6,
                    left: editingTextLayer.x - 6,
                    width: editingTextLayer.width + 12,
                    minHeight: getTextEditorMinHeight(editingTextLayer),
                    fontSize: editingTextLayer.fontSize,
                    lineHeight: editingTextLayer.lineHeight,
                    fontFamily: editingTextLayer.fontFamily,
                    fontWeight: editingTextLayer.fontWeight,
                    letterSpacing: `${editingTextLayer.letterSpacing}px`,
                    textAlign: editingTextLayer.align,
                    color: editingTextLayer.fill,
                    opacity: editingTextLayer.opacity,
                    transform: `rotate(${editingTextLayer.rotation}deg)`,
                  }}
                  onChange={(event) =>
                    updateLayerById(editingTextLayer.id, (layer) => {
                      if (layer.type !== 'text') {
                        return;
                      }
                      layer.text = event.target.value;
                    })
                  }
                  onBlur={finishTextEditing}
                  onKeyDown={(event) => {
                    if (event.key === 'Escape') {
                      event.preventDefault();
                      finishTextEditing();
                    }
                  }}
                />
              ) : null}
            </div>
          </div>
        </section>
      </main>

      <aside className="inspector">
        <section className="panel inspector-shell">
          <div className="inspector-toolbar">
            <div className="inspector-toolbar__meta">
              <span className="panel-kicker">Active Menu</span>
              <strong>{activeMenuMeta.label}</strong>
            </div>
            <div className="action-strip action-strip--compact">
              <button type="button" className="toolbar-btn" disabled={!canUndo} onClick={undo}>
                撤销
              </button>
              <button type="button" className="toolbar-btn" disabled={!canRedo} onClick={redo}>
                重做
              </button>
              <button type="button" className="toolbar-btn" disabled={!selectedLayer} onClick={duplicateSelectedLayer}>
                复制
              </button>
              <button type="button" className="toolbar-btn" disabled={!selectedLayer} onClick={removeSelectedLayer}>
                删除
              </button>
            </div>
          </div>
          <div className="menu-tabs">
            {MENU_ITEMS.map((item) => (
              <button
                key={item.id}
                type="button"
                className={`menu-tab ${activeMenu === item.id ? 'is-active' : ''}`}
                onClick={() => setMenu(item.id)}
              >
                <strong>{item.label}</strong>
                <small>{item.note}</small>
              </button>
            ))}
          </div>

          {!((activeMenu === 'copy' && selectedLayer?.type === 'text') || (activeMenu === 'media' && selectedLayer?.type === 'image')) ? (
            <div className="menu-context">
              <span className="panel-kicker">{activeMenuMeta.note}</span>
              <strong>{inspectorTitle}</strong>
              <p className="summary summary--compact">{inspectorDescription}</p>
            </div>
          ) : null}

          <div className="menu-body">{renderMenuContent()}</div>
        </section>
      </aside>
    </div>
  );
}
