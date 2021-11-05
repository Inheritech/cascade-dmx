# Cascade DMX

This is a DMX through Art-Net (I think? Not sure which one goes first lol) implementation for some WS2812B LED strips that I had. I was using a propietary protocol
but decided that writting lots of integration applications (Almost 10 to the moment of this commit) was not efficient and was tiring and boring, so I just upgraded
my controller code to instead use DMX so I can use Resolume Arena and Unreal Engine on these.

The code is written specifically for my use case using ESP-IDF and its toolchain (Through the Espressif extension for VS Code) and using the [FastLED-idf](https://github.com/bbulkow/FastLED-idf) repository (Under the MIT License attached as FastLED-idf.LICENSE), you might find this helpful as a starter for making your DIY LED strips DMX compatible

You should be able to simply open this with VS Code and the espressif extension and be good to go.

## Thanks

Just want to thank GitHub Copilot for helping write some of the socket code (Saved me the trip to Stack Overflow).
Also the creator of FastLED and the awesome human that ported it to IDF so I don't have to write an awful long component manifest.

## Why Cascade?

That's just the name I gave my personal lighting/VJing project and all the other code (Like the propietary implementation) I wrote for it

## LICENSE

Just like the repositories I used for making this possible, this repository uses the MIT License