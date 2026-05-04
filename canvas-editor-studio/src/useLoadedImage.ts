import { useEffect, useState } from 'react';

export function useLoadedImage(src?: string) {
  const [image, setImage] = useState<HTMLImageElement | null>(null);

  useEffect(() => {
    if (!src) {
      setImage(null);
      return;
    }

    const nextImage = new window.Image();
    nextImage.crossOrigin = 'anonymous';
    nextImage.onload = () => setImage(nextImage);
    nextImage.src = src;

    return () => {
      setImage((currentImage) => (currentImage === nextImage ? null : currentImage));
    };
  }, [src]);

  return image;
}
