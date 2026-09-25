# payload-electrical
PCBs for payload 

## Directory

0_PAY_3D_Model_Library, 0_PAY_Footprint_Library.pretty, 0_PAY_Symbol_Library are libraries for all payload PCBs to use. Use the convention ${KIPRJMOD}/../0_PAY_Symbol_Library/(symbol file) so that library paths are referenced relative to the project path rather than absolute to your specific computer's filesystem.

All PIB and related designs to actually be mounted on satellite should be created as projects in the repo base directory. Any test articles should be created in the test-articles folder. 

simulation should include any relevant MATLAB simulation or spreadsheeting.