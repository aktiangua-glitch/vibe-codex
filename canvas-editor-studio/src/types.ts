export type BackgroundFit = 'cover' | 'contain';
export type HorizontalAlign = 'left' | 'center' | 'right';

export interface BackgroundState {
  color: string;
  imageSrc?: string;
  overlayColor: string;
  overlayOpacity: number;
  imageOpacity: number;
  fit: BackgroundFit;
}

interface BaseLayer {
  id: string;
  type: 'text' | 'image';
  name: string;
  x: number;
  y: number;
  rotation: number;
  opacity: number;
  visible: boolean;
  locked: boolean;
}

export interface TextLayer extends BaseLayer {
  type: 'text';
  text: string;
  width: number;
  fontSize: number;
  fontFamily: string;
  fontWeight: number;
  fill: string;
  align: HorizontalAlign;
  lineHeight: number;
  letterSpacing: number;
}

export interface ImageLayer extends BaseLayer {
  type: 'image';
  src: string;
  width: number;
  height: number;
  stroke: string;
  strokeWidth: number;
  shadowColor: string;
  shadowBlur: number;
  shadowOpacity: number;
}

export type EditorLayer = TextLayer | ImageLayer;

export interface EditorDocument {
  canvas: {
    width: number;
    height: number;
  };
  background: BackgroundState;
  layers: EditorLayer[];
}

export interface ScenePreset {
  id: 'atelier' | 'launch' | 'signal';
  name: string;
  kicker: string;
  label: string;
  hint: string;
  thumbnail: string;
  document: EditorDocument;
}
