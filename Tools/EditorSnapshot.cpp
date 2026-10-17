// Renders the plug-in editor, at its default state, to a PNG: the README
// screenshot (Docs/screenshots/maremba.png) and the render tier's check.
//
//   MarembaEditorSnapshot <output.png>
//
// Fails unless the image has the editor's size and is not blank.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <JuceHeader.h>

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: MarembaEditorSnapshot <output.png>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    MarembaAudioProcessor processor;
    const std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    // A fixed scale, so the image does not depend on the display it ran on.
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);

    if (image.getWidth() != editor->getWidth() * 2 || image.getHeight() != editor->getHeight() * 2) {
        std::fprintf(stderr, "FAIL: snapshot is %dx%d, not twice the %dx%d editor\n", image.getWidth(),
                     image.getHeight(), editor->getWidth(), editor->getHeight());
        return 1;
    }
    // Not blank: the background, the panels and the knobs give several colours.
    juce::SortedSet<juce::uint32> colours;
    for (int y = 0; y < image.getHeight(); y += 7)
        for (int x = 0; x < image.getWidth(); x += 7)
            colours.add(image.getPixelAt(x, y).getARGB());
    if (colours.size() < 16) {
        std::fprintf(stderr, "FAIL: snapshot looks blank (%d colours)\n", colours.size());
        return 1;
    }

    const juce::File output = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    output.getParentDirectory().createDirectory();
    output.deleteFile();
    juce::FileOutputStream stream(output);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("Wrote %s (%dx%d)\n", output.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
