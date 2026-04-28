# TODO

- Implement Frame Encoder
  - Use SRLEN to encode line, run, selector and P2, P4 and P8 streams
- SRLEN encoding
  - Split the analysis and encode step into separate functions since the analysis step actually easily
    can compute the final size of the encoded stream, which might be helpful for the user to prepare the output buffer

